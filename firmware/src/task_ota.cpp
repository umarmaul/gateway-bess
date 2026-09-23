#include "task_ota.h"
#include <Arduino.h>
#include <atomic>
#include <time.h>
#include <string.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <mbedtls/sha256.h>
// secrets.h SEBELUM config.h: OTA_ED25519_PUBKEY_B64 dipertahankan #ifndef di
// config.h -- kalau secrets.h (bench) mendefinisikannya duluan, default kunci
// tim di config.h otomatis dilewati. Urutan sebaliknya membuat timpaan bench
// tidak pernah berlaku.
#include "secrets.h"
#include "config.h"
#include <sodium.h>
#include "mqtt_link.h"
#include "ota_logic.h"

// verifyRollbackLater() adalah symbol WEAK bertipe C biasa (esp32-hal-misc.c,
// bukan C++) -- HARUS extern "C" di sini, kalau tidak nama akan di-mangle
// compiler C++ dan initArduino() tetap memakai default bawaan (false, yaitu
// auto-mark-valid seketika boot, meniadakan seluruh mekanisme rollback di
// bawah). true = tunda mark-valid sampai otaOnMqttConnected() pertama kali.
extern "C" bool verifyRollbackLater() { return true; }

// ---------------------------------------------------------------------------
// State job (satu-satunya job aktif dalam satu waktu -- flow-control server
// menjamin cuma satu chunk in-flight, dan manifest kedua ditolak "busy").
// ---------------------------------------------------------------------------
struct OtaJob {
    bool active = false;
    char id[OTA_MANIFEST_ID_MAX + 1] = "";
    char version[32] = "";
    char sha256_hex[65] = "";
    uint32_t image_size = 0;
    uint32_t chunk_count = 0;
    uint32_t next_index = 0;
    uint32_t received_bytes = 0;
    uint8_t sha256_expected[32] = {};
    const esp_partition_t* partition = nullptr;
    esp_ota_handle_t handle = 0;
    mbedtls_sha256_context sha_ctx{};
    uint32_t last_activity_ms = 0;
};
static OtaJob s_job;
static std::atomic<bool> s_in_progress{false};
static std::atomic<bool> s_mqtt_connected_pending{false};

static char s_gw[13] = "";
static char s_running_label[16] = "";
static uint32_t s_boot_ms = 0;
static bool s_rollback_marked = false;   // sekali per boot: mark-valid ATAU dipastikan bukan PENDING_VERIFY
static bool s_status_reported = false;   // sekali per boot: status persisted (installed/failed) sudah dipublikasikan

struct RawOtaMsg { bool is_chunk; char json[OTA_JSON_MAX + 1]; size_t len; };
static QueueHandle_t q_ota = nullptr;

static SemaphoreHandle_t s_info_mtx = nullptr;
static OtaInfo s_info{};

static uint32_t nowTs() { return (uint32_t)time(nullptr); }

// ---------------------------------------------------------------------------
// snapshot data.ota (dibaca loop() lewat otaGetInfo)
// ---------------------------------------------------------------------------
static bool pendingVerifyNow() {
    esp_ota_img_states_t st;
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!running || esp_ota_get_state_partition(running, &st) != ESP_OK) return false;
    return st == ESP_OTA_IMG_PENDING_VERIFY;
}

static void setInfo(const char* state, const char* id) {
    if (!s_info_mtx) return;
    xSemaphoreTake(s_info_mtx, portMAX_DELAY);
    strncpy(s_info.state, state, sizeof(s_info.state) - 1); s_info.state[sizeof(s_info.state) - 1] = 0;
    strncpy(s_info.id, id, sizeof(s_info.id) - 1); s_info.id[sizeof(s_info.id) - 1] = 0;
    strncpy(s_info.running_partition, s_running_label, sizeof(s_info.running_partition) - 1);
    s_info.running_partition[sizeof(s_info.running_partition) - 1] = 0;
    s_info.pending_verify = pendingVerifyNow();
    xSemaphoreGive(s_info_mtx);
}

void otaGetInfo(OtaInfo& out) {
    if (!s_info_mtx) { out = OtaInfo{}; return; }
    xSemaphoreTake(s_info_mtx, portMAX_DELAY);
    out = s_info;
    xSemaphoreGive(s_info_mtx);
}

bool otaInProgress() { return s_in_progress; }

// ---------------------------------------------------------------------------
// kunci publik Ed25519 (base64 -> 32 byte biner, di-cache setelah percobaan
// pertama -- gagal decode/kunci salah panjang berarti SEMUA manifest ditolak
// signature_invalid, gagal tertutup, bukan gagal terbuka).
// ---------------------------------------------------------------------------
static bool pubkeyBytes(unsigned char out[32]) {
    static unsigned char cached[32];
    static bool ok = false, tried = false;
    if (!tried) {
        tried = true;
        size_t bin_len = 0;
        const char* b64_end = nullptr;
        int r = sodium_base642bin(cached, sizeof(cached), OTA_ED25519_PUBKEY_B64,
                                   strlen(OTA_ED25519_PUBKEY_B64), nullptr, &bin_len,
                                   &b64_end, sodium_base64_VARIANT_ORIGINAL);
        ok = (r == 0 && bin_len == 32);
        if (!ok) Serial.println("[ota] KUNCI PUBLIK OTA_ED25519_PUBKEY_B64 tidak valid (bukan 32 byte base64)");
    }
    if (ok) memcpy(out, cached, 32);
    return ok;
}

// ---------------------------------------------------------------------------
// ack / status
// ---------------------------------------------------------------------------
static void ackManifest(const char* id, const char* result, const char* detail) {
    char buf[OTA_STATUS_JSON_MAX];
    size_t n = otaBuildAck(id, false, 0, result, detail, 0, 0, nowTs(), buf, sizeof(buf));
    if (n) mqttPublish(MQTT_TOPIC_OTA_ACK, buf, n, false);
}

static void ackChunk(const char* id, uint32_t index, const char* result, const char* detail,
                      uint32_t received_bytes, uint32_t next_index) {
    char buf[OTA_STATUS_JSON_MAX];
    size_t n = otaBuildAck(id, true, index, result, detail, received_bytes, next_index, nowTs(), buf, sizeof(buf));
    if (n) mqttPublish(MQTT_TOPIC_OTA_ACK, buf, n, false);
}

static void publishStatus(const char* state, const char* id, const char* version,
                           const char* sha256_hex, uint32_t received_bytes, const char* detail) {
    OtaStatusFields f{};
    f.id = id; f.state = state; f.image_type = OTA_IMAGE_TYPE; f.hardware = OTA_HARDWARE_ID;
    f.gateway_id = s_gw; f.gateway_firmware_version = version;
    f.running_gateway_firmware_version = FW_VERSION;
    f.running_partition = s_running_label;
    f.sha256 = sha256_hex; f.received_bytes = received_bytes; f.detail = detail; f.ts = nowTs();
    char buf[OTA_STATUS_JSON_MAX];
    size_t n = otaBuildStatus(f, buf, sizeof(buf));
    if (n) mqttPublish(MQTT_TOPIC_OTA_STATUS, buf, n, true);   // status SELALU retained
}

// ---------------------------------------------------------------------------
// job lifecycle
// ---------------------------------------------------------------------------
static void resetJobIdle() {
    s_job = OtaJob{};
    s_in_progress = false;
    setInfo("idle", "");
}

// esp_ota_abort aman dipanggil selama esp_ota_end() BELUM sukses/gagal
// dipanggil untuk handle ini -- dipakai untuk kegagalan sebelum finalisasi.
static void failJob(const char* detail) {
    if (s_job.handle) esp_ota_abort(s_job.handle);
    Serial.printf("[ota] job gagal: %s\n", detail);
    publishStatus("failed", s_job.id, s_job.version, s_job.sha256_hex, s_job.received_bytes, detail);
    resetJobIdle();
}

// Dipakai SETELAH esp_ota_end() dipanggil (sukses atau gagal): handle sudah
// tidak valid lagi menurut dokumentasi esp_ota_ops.h, esp_ota_abort() di
// sini adalah undefined behaviour.
static void failJobNoAbort(const char* detail) {
    Serial.printf("[ota] job gagal (pasca esp_ota_end): %s\n", detail);
    publishStatus("failed", s_job.id, s_job.version, s_job.sha256_hex, s_job.received_bytes, detail);
    resetJobIdle();
}

static void finalizeJob() {
    if (s_job.received_bytes != s_job.image_size) { failJob("size_mismatch"); return; }
    uint8_t digest[32];
    mbedtls_sha256_finish(&s_job.sha_ctx, digest);
    if (memcmp(digest, s_job.sha256_expected, 32) != 0) { failJob("sha256_mismatch"); return; }

    publishStatus("verifying", s_job.id, s_job.version, s_job.sha256_hex, s_job.received_bytes, "");
    setInfo("verifying", s_job.id);

    if (esp_ota_end(s_job.handle) != ESP_OK) { failJobNoAbort("ota_end_failed"); return; }
    if (esp_ota_set_boot_partition(s_job.partition) != ESP_OK) { failJobNoAbort("set_boot_partition_failed"); return; }

    // Record NVS dipakai SETELAH reboot (lihat otaOnMqttConnected) untuk
    // membuktikan boot benar-benar terjadi di partisi baru -- Update.end()
    // sukses tidak cukup, harus dicek pasca-reboot (lihat README §OTA).
    Preferences p;
    if (p.begin("mqtt_ota", false)) {
        p.putString("id", s_job.id);
        p.putString("version", s_job.version);
        p.putString("sha256", s_job.sha256_hex);
        p.putString("partition", s_job.partition->label);
        p.putBool("reported", false);
        p.end();
    } else {
        Serial.println("[ota] NVS 'mqtt_ota' tak terbuka -- status pasca-reboot tak bisa diverifikasi");
    }

    publishStatus("restarting", s_job.id, s_job.version, s_job.sha256_hex, s_job.received_bytes, "");
    setInfo("restarting", s_job.id);
    s_in_progress = false;
    s_job.active = false;
    Serial.println("[ota] restart dalam 1 detik");
    delay(1000);
    esp_restart();
}

// ---------------------------------------------------------------------------
// manifest / chunk masuk
// ---------------------------------------------------------------------------
static void handleManifest(const char* json, size_t len) {
    if (s_job.active) {
        // busy: TANPA publish status (job berjalan tetap dilaporkan apa
        // adanya) -- coba ambil id dari manifest yang ditolak supaya ack
        // tetap informatif, tapi tidak wajib berhasil.
        OtaManifest tmp{};
        otaParseManifest(json, len, tmp);
        ackManifest(tmp.id, "rejected", "busy");
        return;
    }
    OtaManifest m{};
    OtaManifestStatus st = otaParseManifest(json, len, m);
    if (st == OtaManifestStatus::INVALID_MANIFEST) {
        ackManifest(m.id, "rejected", "invalid_manifest");
        publishStatus("failed", m.id, m.version, m.sha256_hex, 0, "invalid_manifest");
        return;
    }
    if (st == OtaManifestStatus::HARDWARE_MISMATCH) {
        ackManifest(m.id, "rejected", "hardware_mismatch");
        publishStatus("failed", m.id, m.version, m.sha256_hex, 0, "hardware_mismatch");
        return;
    }
    unsigned char pk[32];
    if (sodium_init() < 0 || !pubkeyBytes(pk) ||
        crypto_sign_verify_detached(m.signature_raw, m.sha256_raw, 32, pk) != 0) {
        ackManifest(m.id, "rejected", "signature_invalid");
        publishStatus("failed", m.id, m.version, m.sha256_hex, 0, "signature_invalid");
        return;
    }
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (!target) {
        ackManifest(m.id, "rejected", "ota_begin_failed");
        publishStatus("failed", m.id, m.version, m.sha256_hex, 0, "no_ota_partition");
        return;
    }
    esp_ota_handle_t handle;
    // Verifikasi tanda tangan SUDAH lulus di atas SEBELUM baris ini -- tidak
    // ada satu byte pun ditulis ke flash untuk manifest yang tidak sah.
    if (esp_ota_begin(target, m.image_size, &handle) != ESP_OK) {
        ackManifest(m.id, "rejected", "ota_begin_failed");
        publishStatus("failed", m.id, m.version, m.sha256_hex, 0, "ota_begin_failed");
        return;
    }

    s_job = OtaJob{};
    strncpy(s_job.id, m.id, sizeof(s_job.id) - 1);
    strncpy(s_job.version, m.version, sizeof(s_job.version) - 1);
    strncpy(s_job.sha256_hex, m.sha256_hex, sizeof(s_job.sha256_hex) - 1);
    s_job.image_size = m.image_size;
    s_job.chunk_count = m.chunk_count;
    memcpy(s_job.sha256_expected, m.sha256_raw, 32);
    s_job.partition = target;
    s_job.handle = handle;
    mbedtls_sha256_init(&s_job.sha_ctx);
    mbedtls_sha256_starts(&s_job.sha_ctx, 0);
    s_job.last_activity_ms = millis();
    s_job.active = true;
    s_in_progress = true;

    ackManifest(m.id, "accepted", "");
    publishStatus("downloading", s_job.id, s_job.version, s_job.sha256_hex, 0, "");
    setInfo("downloading", s_job.id);
}

static void handleChunk(const char* json, size_t len) {
    char id[OTA_MANIFEST_ID_MAX + 1]; uint32_t index = 0;
    char b64[OTA_CHUNK_MAX_BASE64 + 1]; size_t b64len = 0;
    if (!otaParseChunkEnvelope(json, len, id, index, b64, b64len)) {
        // Amplop tak bisa diurai (JSON rusak / field hilang) -- job mana yang
        // dimaksud tak diketahui pasti. DEVIASI dari kontrak tim yang tidak
        // mendaftar kasus ini secara eksplisit: dipetakan ke "unexpected_chunk"
        // (nilai detail paling umum untuk "chunk ini tidak bisa diterima"),
        // didokumentasikan di README/CHANGELOG.
        ackChunk(s_job.active ? s_job.id : "", 0, "rejected", "unexpected_chunk",
                 s_job.received_bytes, s_job.next_index);
        return;
    }

    OtaChunkOutcome outcome = otaEvaluateChunk(s_job.active ? s_job.id : "", s_job.chunk_count,
                                                s_job.next_index, id, index);
    switch (outcome) {
        case OtaChunkOutcome::WRONG_JOB:
            ackChunk(id, index, "rejected", "wrong_job", 0, 0);
            return;
        case OtaChunkOutcome::STALE:
            ackChunk(id, index, "rejected", "stale_chunk", s_job.received_bytes, s_job.next_index);
            return;
        case OtaChunkOutcome::UNEXPECTED:
            ackChunk(id, index, "rejected", "unexpected_chunk", s_job.received_bytes, s_job.next_index);
            return;
        case OtaChunkOutcome::DUPLICATE:
            // idempotent: chunk yang sama sudah tertulis, aman untuk retry QoS1.
            ackChunk(id, index, "accepted", "duplicate", s_job.received_bytes, s_job.next_index);
            return;
        case OtaChunkOutcome::NEW:
            break;
    }

    uint8_t buf[OTA_CHUNK_MAX_BYTES]; size_t n = 0;
    OtaDataStatus ds = otaDecodeChunkData(b64, b64len, buf, sizeof(buf), n);
    if (ds == OtaDataStatus::EMPTY || ds == OtaDataStatus::TOO_LONG) {
        // chunk ini ditolak tapi job TETAP jalan -- server boleh mengirim ulang.
        ackChunk(id, index, "rejected", "unexpected_chunk", s_job.received_bytes, s_job.next_index);
        return;
    }
    if (ds == OtaDataStatus::INVALID_BASE64 || ds == OtaDataStatus::OVERSIZED) {
        // base64 rusak / oversize: job langsung gagal (kontrak tim), bukan
        // sekadar reject chunk -- lanjut dari manifest baru wajib.
        ackChunk(id, index, "rejected", "invalid_chunk_data", s_job.received_bytes, s_job.next_index);
        failJob("invalid_chunk_data");
        return;
    }
    if (esp_ota_write(s_job.handle, buf, n) != ESP_OK) {
        ackChunk(id, index, "rejected", "write_failed", s_job.received_bytes, s_job.next_index);
        failJob("write_failed");
        return;
    }
    mbedtls_sha256_update(&s_job.sha_ctx, buf, n);
    s_job.received_bytes += (uint32_t)n;
    s_job.next_index++;
    s_job.last_activity_ms = millis();
    ackChunk(id, index, "accepted", "", s_job.received_bytes, s_job.next_index);
    setInfo("downloading", s_job.id);

    if (s_job.next_index == s_job.chunk_count) finalizeJob();
}

static void checkJobTimeout() {
    if (!s_job.active) return;
    if (millis() - s_job.last_activity_ms > OTA_JOB_TIMEOUT_MS) failJob("timeout");
}

// ---------------------------------------------------------------------------
// rollback + status persisted (dipicu MQTT_EVENT_CONNECTED via otaOnMqttConnected)
// ---------------------------------------------------------------------------
static void markValidIfPending() {
    if (s_rollback_marked) return;
    if (pendingVerifyNow()) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
            Serial.println("[ota] image ditandai valid (rollback dibatalkan)");
        else
            Serial.println("[ota] esp_ota_mark_app_valid_cancel_rollback gagal");
    }
    s_rollback_marked = true;   // sekali coba per boot, apa pun hasilnya
}

static void reportPersistedStatusOnce() {
    if (s_status_reported) return;
    s_status_reported = true;   // ditandai duluan: gagal baca NVS tak boleh mengulang tiap reconnect
    Preferences p;
    if (!p.begin("mqtt_ota", true)) return;   // tak pernah OTA -- tak ada apa pun dilaporkan
    if (!p.isKey("id")) { p.end(); return; }
    char id[OTA_MANIFEST_ID_MAX + 1] = "", version[32] = "", sha[65] = "", partition[16] = "";
    p.getString("id", id, sizeof(id));
    p.getString("version", version, sizeof(version));
    p.getString("sha256", sha, sizeof(sha));
    p.getString("partition", partition, sizeof(partition));
    bool reported = p.getBool("reported", false);
    p.end();
    if (reported) return;   // sudah dilaporkan boot sebelumnya (mis. reconnect cepat, RTC reset)

    bool match = strcmp(partition, s_running_label) == 0;
    publishStatus(match ? "installed" : "failed", id, version, sha, 0,
                  match ? "" : "boot_partition_mismatch");

    // Tandai sudah dilapor supaya tak terulang tiap reconnect (status tetap
    // RETAINED di broker, jadi subscriber baru masih melihatnya).
    Preferences p2;
    if (p2.begin("mqtt_ota", false)) { p2.putBool("reported", true); p2.end(); }
}

void otaOnMqttConnected() { s_mqtt_connected_pending = true; }

// ---------------------------------------------------------------------------
// task
// ---------------------------------------------------------------------------
static void submit(bool is_chunk, const char* json, size_t n) {
    // static: ~2 KB terlalu besar untuk stack task esp-mqtt (event handler
    // yang memanggil ini). Aman: hanya dipanggil dari task esp-mqtt, satu
    // pesan diproses sekaligus, dan xQueueSend menyalin isinya sebelum kembali.
    static RawOtaMsg raw;
    raw.is_chunk = is_chunk;
    raw.len = n > OTA_JSON_MAX ? OTA_JSON_MAX : n;
    memcpy(raw.json, json, raw.len);
    raw.json[raw.len] = 0;
    if (xQueueSend(q_ota, &raw, 0) != pdTRUE)
        Serial.println("[ota] dibuang: antrean task_ota penuh");
}

void taskOtaSubmitManifest(const char* json, size_t n) { submit(false, json, n); }
void taskOtaSubmitChunk(const char* json, size_t n) { submit(true, json, n); }

static void run(void*) {
    // Diawasi task watchdog (temuan audit 23 Sep 2026): selama job aktif,
    // task_cmd menolak SEMUA command dengan ota_in_progress -- kalau task ini
    // macet tanpa jaring pengaman, kendali jarak jauh BESS hilang sampai
    // power-cycle. Operasi terpanjangnya (erase partisi di esp_ota_begin,
    // <=1,9 MB, puluhan detik) masih jauh di bawah WDT_TIMEOUT_S=120 dtk; semua
    // publish lewat antrean mqtt_tx (tak pernah menunggu lock esp-mqtt).
    esp_task_wdt_add(nullptr);
    s_info_mtx = xSemaphoreCreateMutex();
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running) strncpy(s_running_label, running->label, sizeof(s_running_label) - 1);
    s_boot_ms = millis();
    setInfo("idle", "");

    RawOtaMsg m;
    for (;;) {
        esp_task_wdt_reset();
        if (xQueueReceive(q_ota, &m, pdMS_TO_TICKS(2000)) == pdTRUE) {
            if (m.is_chunk) handleChunk(m.json, m.len);
            else handleManifest(m.json, m.len);
        }
        if (s_mqtt_connected_pending.exchange(false)) {
            markValidIfPending();
            reportPersistedStatusOnce();
        }
        checkJobTimeout();
        // 15 menit sejak BOOT (bukan sejak job) tanpa MQTT tersambung sekali
        // pun sementara image masih PENDING_VERIFY -> anggap boot ini gagal,
        // paksa rollback ke image lama. markValidIfPending() di atas sudah
        // menandai s_rollback_marked begitu tersambung, jadi baris ini hanya
        // pernah benar-benar mem-boot-ulang kalau MQTT TAK PERNAH tersambung.
        if (!s_rollback_marked && millis() - s_boot_ms > OTA_ROLLBACK_PENDING_MS && pendingVerifyNow()) {
            Serial.println("[ota] 15 menit tanpa MQTT tersambung dgn image PENDING_VERIFY -> rollback paksa");
            esp_ota_mark_app_invalid_rollback_and_reboot();   // tak pernah kembali kalau berhasil
            s_rollback_marked = true;   // jaga-jaga kalau baris di atas gagal
        }
    }
}

void taskOtaStart(const char* gw) {
    strncpy(s_gw, gw, sizeof(s_gw) - 1);
    q_ota = xQueueCreate(OTA_QUEUE_LEN, sizeof(RawOtaMsg));
    xTaskCreate(run, "task_ota", 8192, nullptr, 2, nullptr);
}

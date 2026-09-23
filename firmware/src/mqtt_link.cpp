#include "mqtt_link.h"
#include <Arduino.h>
#include <mqtt_client.h>
#include <atomic>
#include "config.h"
#include "secrets.h"
#include "task_cmd.h"
#include "task_ota.h"

static esp_mqtt_client_handle_t cli = nullptr;
// Ditulis task esp-mqtt, dibaca loop() dan task_cmd — atomic supaya
// compiler tidak men-cache nilainya di register lintas task.
static std::atomic<bool> connected{false};
static char t_telemetry[48], t_status[48], t_command[48], t_ack[52];
static char t_ota_manifest[56], t_ota_chunk[56], t_ota_ack[56], t_ota_status[56];
static TaskHandle_t tx_task = nullptr;

static void onEvent(void*, esp_event_base_t, int32_t event_id, void* event_data) {
    auto* e = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            connected = true;
            esp_mqtt_client_publish(cli, t_status, "online", 0, 1, 1);
            esp_mqtt_client_subscribe(cli, t_command, 1);
            esp_mqtt_client_subscribe(cli, t_ota_manifest, 1);
            esp_mqtt_client_subscribe(cli, t_ota_chunk, 1);
            otaOnMqttConnected();            // mark-valid + status persisted (sekali per boot)
            if (tx_task) xTaskNotifyGive(tx_task);   // kuras pesan yang tertahan
            Serial.println("[mqtt] connected");
            break;
        case MQTT_EVENT_DISCONNECTED:
            connected = false;
            Serial.println("[mqtt] disconnected");
            break;
        case MQTT_EVENT_DATA: {
            // esp-mqtt memotong pesan yang lebih besar dari buffer masuk.
            // Potongan pertama BUKAN JSON utuh — memprosesnya menghasilkan
            // ack bad_json yang menyesatkan. Hanya proses pesan lengkap.
            bool utuh = e->current_data_offset == 0 &&
                        e->data_len == e->total_data_len;
            if (!utuh) {
                Serial.printf("[mqtt] pesan terpotong diabaikan (%d/%d B)\n",
                              e->data_len, e->total_data_len);
                break;
            }
            if (e->topic_len == (int)strlen(t_command) &&
                !strncmp(e->topic, t_command, e->topic_len))
                taskCmdSubmit(e->data, e->data_len);
            else if (e->topic_len == (int)strlen(t_ota_manifest) &&
                     !strncmp(e->topic, t_ota_manifest, e->topic_len))
                taskOtaSubmitManifest(e->data, e->data_len);
            else if (e->topic_len == (int)strlen(t_ota_chunk) &&
                     !strncmp(e->topic, t_ota_chunk, e->topic_len))
                taskOtaSubmitChunk(e->data, e->data_len);
            break;
        }
        default: break;
    }
}

void mqttInit(const char* gw) {
    snprintf(t_telemetry, sizeof(t_telemetry), "device/%s/telemetry", gw);
    snprintf(t_status, sizeof(t_status), "device/%s/status", gw);
    snprintf(t_command, sizeof(t_command), "device/%s/command", gw);
    snprintf(t_ack, sizeof(t_ack), "device/%s/command/ack", gw);
    snprintf(t_ota_manifest, sizeof(t_ota_manifest), "device/%s/ota/manifest", gw);
    snprintf(t_ota_chunk, sizeof(t_ota_chunk), "device/%s/ota/chunk", gw);
    snprintf(t_ota_ack, sizeof(t_ota_ack), "device/%s/ota/ack", gw);
    snprintf(t_ota_status, sizeof(t_ota_status), "device/%s/ota/status", gw);
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = MQTT_URI;
    // client_id = MAC, sama dengan BEPESP32_WiFi_Extension. Tanpa ini esp-mqtt
    // memakai default "ESP32_xxxxxx" → ACL broker bergaya device/${clientid}/#
    // akan menolak publish ke topic kita sendiri.
    cfg.credentials.client_id = gw;
    cfg.credentials.username = MQTT_USER;
    cfg.credentials.authentication.password = MQTT_PASSWD;
    cfg.session.keepalive = MQTT_KEEPALIVE_S;
    cfg.network.timeout_ms = MQTT_NETWORK_TIMEOUT_MS;
    cfg.buffer.out_size = MQTT_WRITE_BUFFER;   // telemetri ~3,3 KB butuh margin
    cfg.buffer.size = MQTT_READ_BUFFER;
    cfg.session.last_will.topic = t_status;
    cfg.session.last_will.msg = "offline";
    cfg.session.last_will.qos = 1;
    cfg.session.last_will.retain = 1;
    cli = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(cli, MQTT_EVENT_ANY, onEvent, nullptr);
    // Belum start: dulu start di sini (sebelum WiFi asosiasi) membuat percobaan
    // connect pertama selalu gagal DNS — log error palsu di setiap boot.
}

void mqttTick(bool wifi_up) {
    static bool started = false;
    if (started || !cli || !wifi_up) return;
    // started hanya diset kalau start benar-benar sukses (mis. heap cukup untuk
    // task esp-mqtt) — kalau gagal, loop() berikutnya mencoba lagi alih-alih
    // MQTT mati permanen sampai reboot.
    if (esp_mqtt_client_start(cli) == ESP_OK) started = true;
    else Serial.println("[mqtt] start gagal, dicoba lagi");
}

bool mqttConnected() { return connected; }

// ---------------------------------------------------------------------------
// Jalur kirim: task mqtt_tx satu-satunya pemanggil esp_mqtt_client_enqueue.
//
// enqueue menunggu MQTT_API_LOCK tanpa batas, dan task esp-mqtt memegang lock
// itu sepanjang satu iterasi — termasuk tulisan parsial yang terus diulang dan
// connect (DNS + TCP + CONNACK), yang pada link tercekik bisa > 120 dtk. Kalau
// loop()/task_cmd/task_ota memanggilnya langsung, mereka ikut tertahan dan
// task watchdog me-reboot gateway padahal hanya lambat. Jadi ketiganya cuma
// menitip (tak pernah menunggu lock), dan mqtt_tx — sengaja TIDAK didaftarkan
// ke watchdog — yang menanggung penantiannya.
//
// Antrean ini GENERIK sejak sub-proyek G: satu FIFO untuk topic ack/ota_ack/
// ota_status (dulu khusus ack). item.retain menentukan flag retain per pesan
// (status OTA retained, ack/ota_ack tidak) -- aturan basi/tahan-sampai-
// terhubung yang sudah ada untuk ack dipertahankan apa adanya untuk ketiganya.
// ---------------------------------------------------------------------------
struct TxItem { MqttTopic topic_id; bool retain; uint32_t t_ms; uint16_t n; char json[MQTT_TX_JSON_MAX]; };
static QueueHandle_t q_tx = nullptr;
static portMUX_TYPE telem_mux = portMUX_INITIALIZER_UNLOCKED;
static char telem_buf[TELEMETRY_JSON_MAX];   // titipan terbaru (latest wins)
static size_t telem_n = 0;
static bool telem_pending = false;

static const char* topicFor(MqttTopic t) {
    switch (t) {
        case MQTT_TOPIC_ACK: return t_ack;
        case MQTT_TOPIC_OTA_ACK: return t_ota_ack;
        case MQTT_TOPIC_OTA_STATUS: return t_ota_status;
    }
    return t_ack;
}

static void txRun(void*) {
    static char out[TELEMETRY_JSON_MAX];
    static TxItem it;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        // Pesan ditahan di antrean kita sampai terhubung, baru diserahkan ke
        // esp-mqtt — outbox esp-mqtt membuang pesan >30 dtk bahkan saat masih
        // menunggu reconnect, jadi titip di sana selagi putus = hilang diam-diam.
        while (connected && xQueuePeek(q_tx, &it, 0) == pdTRUE) {
            xQueueReceive(q_tx, &it, 0);
            if (millis() - it.t_ms > TX_MAX_AGE_MS) {
                Serial.println("[mqtt] pesan kedaluwarsa dibuang (tertahan > TX_MAX_AGE_MS)");
                continue;
            }
            const char* topic = topicFor(it.topic_id);
            if (esp_mqtt_client_enqueue(cli, topic, it.json, it.n, 1, it.retain ? 1 : 0, true) < 0)
                Serial.printf("[mqtt] pesan ke %s ditolak outbox esp-mqtt\n", topic);
        }
        size_t n = 0;
        portENTER_CRITICAL(&telem_mux);
        if (telem_pending && connected) {
            memcpy(out, telem_buf, telem_n);
            n = telem_n;
            telem_pending = false;
        }
        portEXIT_CRITICAL(&telem_mux);
        if (n && esp_mqtt_client_enqueue(cli, t_telemetry, out, n, 1, 0, true) < 0)
            Serial.println("[mqtt] telemetri ditolak outbox esp-mqtt");
    }
}

void mqttTxStart() {
    q_tx = xQueueCreate(TX_QUEUE_LEN, sizeof(TxItem));
    xTaskCreate(txRun, "mqtt_tx", 4096, nullptr, 1, &tx_task);
}

// n == 0 berarti builder gagal (buffer kurang). esp-mqtt menafsirkan len 0
// sebagai "hitung strlen sendiri", jadi tanpa penjaga ini isi buffer yang
// tidak valid ikut terkirim. Tolak di sini untuk kedua jalur.
bool mqttEnqueueTelemetry(const char* json, size_t n) {
    if (!tx_task || n == 0 || n > sizeof(telem_buf)) return false;
    // memcpy ~3,5 KB di critical section: ~puluhan µs, tanpa menunggu apa pun.
    portENTER_CRITICAL(&telem_mux);
    memcpy(telem_buf, json, n);
    telem_n = n;
    telem_pending = true;
    portEXIT_CRITICAL(&telem_mux);
    xTaskNotifyGive(tx_task);
    return true;
}

bool mqttPublish(MqttTopic topic, const char* json, size_t n, bool retain) {
    if (!tx_task || n == 0 || n > MQTT_TX_JSON_MAX) return false;
    // TIDAK static: dipanggil dari task_cmd DAN task_ota, bisa benar-benar
    // bersamaan (dua task berbeda) -- variabel statik bersama akan balapan
    // saat kedua task menulis sebelum xQueueSend sempat menyalinnya. Item ini
    // hanya perlu hidup selama panggilan (xQueueSend menyalin isinya), jadi
    // aman di stack task pemanggil (~1 KB, task_cmd/task_ota punya ruang).
    TxItem it;
    it.topic_id = topic;
    it.retain = retain;
    it.t_ms = millis();
    it.n = (uint16_t)n;
    memcpy(it.json, json, n);
    if (xQueueSend(q_tx, &it, 0) != pdTRUE) return false;   // antrean penuh
    xTaskNotifyGive(tx_task);
    return true;
}

bool mqttPublishAck(const char* json, size_t n) {
    return mqttPublish(MQTT_TOPIC_ACK, json, n, false);
}

# SDD ledger — plan: docs/superpowers/plans/2026-08-13-fondasi-paritas.md

Branch: fondasi-paritas-13aug (bukan worktree terpisah — `firmware/src/secrets.h`
di-gitignore, jadi worktree baru tidak punya kredensial WiFi/MQTT dan seluruh
langkah bench akan gagal).
Base branch: master. Commit awal branch: 55b77b9 (5 perbaikan audit) .

Task 1: complete (commits 55b77b9..be760d3, review clean)
  - Reviewer memverifikasi ulang aritmetika partisi: menutup 0x9000..0x400000 tanpa
    celah/tumpang tindih, app0 = 1.966.080 B. Byte-per-byte sama dengan referensi.
  - ⚠️ reviewer "cannot verify from diff": keluaran build/boot hanya ada di laporan.
    Diselesaikan controller: build sesudahnya melaporkan denominator 1966080 (56,2%),
    naik dari 1310720 — konsisten, bukan celah.

Task 2: review 1 — spec ❌ (USB/JTAG hilang dari tabel nama, spec §3.2 menyebutnya
  eksplisit) + 2 Important (static_assert cuma menjaga 6 dari 11 konstanta;
  kegagalan NVS begin() senyap). Fix round 1 dikirim ke implementer asal.
Task 2: minor (deferred): putUInt NVS tiap boot tanpa rate-limit (wear-leveled, aman)
Task 2: minor (deferred): tes resetReasonName hanya menguji 4 dari 11 nilai terpetakan
Task 2: minor (deferred): cabang `else if (reason == RESET_UNKNOWN)` redundan dgn NAMA[0]
Task 2: catatan — "reboot loop" yang sempat terlihat di bench ternyata ulah sesi
  `pio device monitor` implementer sendiri (toggle DTR/RTS me-reset board).
  Capture 120 dtk tanpa DTR/RTS: nol reboot, boot_count stabil. PENTING untuk Task 3 —
  jangan pakai pio device monitor saat mengukur reboot; pakai pyserial dtr/rts=False.
Task 2: fix round 1/5 (3 addressed, 0 open; commits 532e93f..a38ea7d)
Task 2: complete (commits be760d3..a38ea7d, review clean)
Task 3: complete (commits a38ea7d..38ee356, review clean) — BELUM TEREPRODUKSI.
  3 percobaan (~11,7 mnt di bawah tekanan), boot_count diam di 12. Kontrol positif:
  reset esptool menaikkan 12->13->14, jadi pencacahnya memang bekerja. Coredump kosong
  TAPI partisinya lahir di be760d3, sesudah kejadian kemarin -> tidak membebaskan apa pun.
  Akar penyebab kejadian kemarin TETAP TIDAK DIKETAHUI. Instrumentasi terpasang.
  Catatan: pulsa DTR/RTS via pyserial TIDAK me-reset board -> melemahkan (bukan membatalkan)
  teori "pio device monitor penyebabnya" dari Task 2.
Task 3: minor (deferred): kick_probe.py — watcher tak di-disconnect eksplisit
Task 3: minor (deferred): kick_probe.py — json.loads dipanggil dua kali di on_msg
Task 3: backlog (bukan temuan): heap/min_heap hanya dicetak saat boot, tidak ada di
  telemetri — kalau kejadian kemarin memori habis, cloud tidak bisa melihatnya.
  Kandidat kuat untuk spec berikutnya.
Task 4: review 1 — spec ✅ (nilai buffer, kondisi penjaga, argumen enqueue cocok
  byte-per-byte dgn MqttManager.cpp referensi) + 2 Important: (a) laporan mengklaim
  +80 B RAM "sesuai ekspektasi +26 KB" padahal tidak — angka RAM PlatformIO statis
  (.data/.bss), buffer esp-mqtt di-malloc di heap saat runtime; (b) verifikasi bench
  tidak menguji perubahan buffer sama sekali (telemetri 3,3 KB sudah jalan sebelum ini).
  Fix round 1 dikirim: uji command ~1500 B (buffer baca) dan ~3000 B (penjaga terpotong).
Task 4: minor (deferred): penjaga terpotong bisa membuang command kecil yang kebetulan
  terbelah antar TCP read tanpa ack — diwarisi apa adanya dari firmware referensi
  (paritas disengaja), bukan cacat baru task ini.
Task 4: fix round 1/5 (2 addressed: RAM dijelaskan benar + komentar mqtt_link.h;
  1 open: bukti bench; commits 366ed90..cf14cc5)
Task 4: koreksi kriteria (controller, bukan temuan dibuang) — kriteria "ack normal untuk
  command ~1500 B" yang SAYA tulis mustahil dipenuhi: taskCmdSubmit menyalin ke
  RawCmd::json[512], jadi command >511 B selalu jadi bad_json. Yang memang mau dibuktikan
  2a = buffer BACA membesar, dan itu TERBUKTI (1501 B tanpa baris "pesan terpotong").
  Fix round 2 hanya menyisakan 2b: buktikan ketiadaan ack dgn listener yang terbukti menunggu.
Task 4: TEMUAN BARU dari uji 2a — taskCmdSubmit memotong command >511 byte diam-diam
  (RawCmd::json[512], memcpy + min()). Command 512..2048 B kini sampai utuh di lapisan
  MQTT lalu dirusak jadi bad_json. Bukan cacat task ini, TIDAK diperbaiki di sini.
  Sekelas dengan Task 7 (jangan membuang perintah diam-diam) tapi DI LUAR spec §4.3 —
  keputusan spec, perlu diangkat ke user di akhir fase.
Task 4: fix round 2/5 (1 addressed, 0 open; tanpa commit baru — bukti bench + laporan)
Task 4: complete (commits 38ee356..cf14cc5, review clean)
  Bukti akhir: 1501 B utuh tanpa baris penjaga (buffer baca 2048 terbukti);
  3004 B terbelah 3 fragmen (2014+776+214) dgn listener 43 dtk terbukti kosong
  (penjaga terpotong terbukti).
Task 5: complete (commits cf14cc5..d462d9b, review clean) — 28/28 native.
  Reviewer menelusuri sendiri: target default 1 saat args/target absen; penolakan
  target!=1 TIDAK membajak BAD_JSON/UNSUPPORTED; ack menggemakan nama yang dikirim cloud.
Task 5: minor (deferred): jalur penolakan target!=1 tak punya tes native (task_cmd.cpp
  bergantung FreeRTOS/Serial/MQTT, di luar target native) — bukan regresi task ini.
Task 5: anomali dicatat implementer — boot_count 15->17 (bukan 16) sesudah satu upload.
  Dugaan wajar: esptool me-reset dua kali (sebelum masuk mode download & sesudah flash),
  jadi app sempat boot dua kali. Tidak dikejar; instrumentasi Task 2 akan menangkap
  kalau ini berulang di luar siklus flash.
Task 5: anomali dicatat implementer — 2 percobaan set_power tanpa ack 20-40 dtk saat
  RSSI -88 dBm, percobaan ke-3 (RSSI lebih baik) sukses. Konsisten dgn isu EMI/WiFi
  yang sudah terdokumentasi di CLAUDE.md root, bukan regresi kode.
Task 6: review 1 — spec ✅ (pemangkasan sampai ke register: simulator terbukti jalan
  +60,00 kW, bukan +70). 1 Important: penjaga NaN asimetris — rated_w dijaga, power_w
  tidak; NaN lolos jadi pct=NaN lalu lroundf(NaN) = UB di jalur tulis REG_P_SET.
  Belum terbukti terjangkau lewat parseCommand (JSON ketat tak punya literal NaN, dan
  infinity sudah dipangkas benar) -> pertahanan berlapis, bukan bug hidup.
  Fix round 1 dikirim: tolak bad_value di doSetPower + guard di planPowerPct + 2 tes.
Task 6: minor (deferred): tidak ada tes di batas persis ±120% (ikut dimasukkan ke fix
  round 1 karena murah dan mengunci perilaku perbandingan ketat yang jadi sandaran fix)
Task 6: minor (catatan positif): formula applied_w lama (raw/1000*rated_w/10*10) diganti
  applied_pct/100*rated_w — identik secara aritmetika, jauh lebih jelas.
Task 6: fix round 1/5 (1 addressed, 0 open; commits aad0737..77b0d4a) — 34/34 native.
Task 6: complete (commits d462d9b..77b0d4a, review clean)
  Tes batas ±120% yang diminta menyingkap masalah presisi nyata: 60000/50000*100 di
  float32 = 120,00000763 > 120 -> permintaan TEPAT di batas dilaporkan clamped.
  Implementer pindah ke double di dalam planPowerPct (di luar instruksi, dilaporkan
  terbuka). Re-reviewer menghitung ulang: error residual double ~1e-13 vs ULP float
  ~7,6e-6 di sekitar 120 -> perbaikan jujur, bukan bug dipindah. Kasus 70 kW terverifikasi
  tetap identik. ESP32-C6 tanpa FPU keras, float pun sudah soft-fp; sekali per command.
  KEPUTUSAN: double dipertahankan.
Task 7: complete (commits 77b0d4a..ecca35d, review clean)
  Bench: 7 enable beruntun -> 5 accepted, 1 queue_full (id cocok), 1 tanpa ack yang
  dikuatkan baris serial "[cmd] dibuang: antrean utama dan luapan penuh".
  Reviewer memverifikasi kapasitas sendiri: 4 antrean + 1 luapan + 1 sedang jalan = 6.
  Penyimpangan spec §5 (tanpa tes native queue_full) DISETUJUI reviewer: buildAckJson
  tidak bercabang, bentuknya sudah dipatok tes lain, jadi tes baru mustahil merah dulu.
Task 7: minor (deferred): invariant "luapan hanya terisi saat antrean utama penuh"
  (yang menjamin ack tidak menggantung selamanya) tidak terbaca dari run() saja —
  layak satu baris komentar.
Task 7: catatan operasional — di Git Bash, kill/pkill TIDAK mematikan proses `uv run
  bess-sim` (PID MSYS != PID Windows). Pakai PowerShell Stop-Process + verifikasi tasklist.
Task 8: review 1 — spec ✅, kedua tes terbukti DECISIVE (keduanya gagal di ekspresi
  lama, jadi bukan tes hampa). 1 Important: next_try_ms tidak pernah disegarkan di
  jalur "sudah terhubung", jadi setelah 24,8-49,7 hari tersambung deadline-nya melewati
  jendela 2^31 ms dan percobaan reconnect pertama sesudah putus gagal dievaluasi
  (sembuh sendiri, tapi bisa berminggu-minggu). Varian dari bug yang sama.
  Fix round 1 dikirim: next_try_ms = millis() di jalur terhubung.
Task 8: minor (deferred): (int32_t)(uint32-uint32) implementation-defined sebelum C++20
  (well-defined sejak C++20); GCC/Clang/MSVC semuanya two's-complement — aman di
  toolchain ini, layak satu baris komentar kalau compiler berganti.
Task 8: minor (deferred): RUN_TEST ditaruh di akhir main(), bukan persis sesudah
  test_plan_power_rated_belum_diketahui seperti bunyi brief — kosmetik.
Task 8: koreksi brief (cacat rencana, bukan implementer): Step 4 brief menulis harapan
  "34 test cases" padahal Step 1 sudah menambah 2 tes -> seharusnya 36. Implementer
  benar mengikuti constraint 34->36.
Task 8: fix round 1/5 (1 addressed, 0 open; commits 341ba5f..3b62a3d)
Task 8: complete (commits ecca35d..3b62a3d, review clean) — 36/36 native.
  Re-reviewer menelusuri sendiri: dgn next_try_ms disegarkan tiap tick terhubung,
  kebasian sekarang dibatasi satu periode loop (~100 ms), bukan lama uptime —
  ~9 orde di dalam jendela 2^31 ms. Ladder backoff tetap jalan mulai tick kedua.
Task 9: complete (commits 3b62a3d..b65500f, review clean)
  Reviewer memastikan urutan evaluasi braced-init-list ([dcl.init.list]) menjamin
  mbReadRegs() ter-sequence SEBELUM pembacaan exc -> tidak ada kode exception yang
  tertukar antar blok. exc juga direset tiap blok, dan hanya dicetak saat MB_EXCEPTION.
Task 9: BIAYA TERUKUR dari menghapus short-circuit (trade yang memang disetujui, tapi
  besarannya baru diketahui sekarang): pollOnce saat BESS mati total ~1,8 dtk -> ~7,2 dtk,
  sehingga deteksi comm_lost ~8-9 dtk -> ~24-25 dtk (3x). Selain itu mb_mtx dipegang
  lebih lama sehingga task_cmd bisa menunggu slot bus sampai ~7,2 dtk (sebelumnya ~1,8).
  PERLU DIANGKAT KE USER di akhir fase — konsekuensi kontrak, bukan cacat.
Task 10: review 1 — spec ✅. Checklist 10 butir dinilai RIGOROUS (bukan diklaim):
  reviewer mengecek sendiri state_machine.py (STAGE_S=1.0) membenarkan penjelasan
  PRECHARGE/RELAY tak terlihat di sampling 2 dtk, dan memastikan boot_count=27
  punya nilai awal DAN akhir yang benar-benar ditangkap.
  1 Important: README:76 masih menulis ack lewat `publish` padahal Task 4 mengubahnya
  jadi enqueue. Minor: kosakata command lama (tanpa set_output) di baris 75/183/188/189/192.
  Fix round 1 dikirim (minor ikut karena tujuan task ini memang menyelaraskan README).
Task 10: fix round 1/5 (2 addressed + 1 celah temuan sendiri: contoh log boot belum
  memuat baris reset=/boot_count=; commits 301771a..e5e175c)
Task 10: complete (commits b65500f..e5e175c, review clean)
  Checklist 10/10 LULUS dari cold reflash, boot_count 27 -> 27 (tidak ada reboot).
SEMUA TASK SELESAI. Lanjut review akhir seluruh branch.

KOREKSI (review akhir seluruh branch, pasca "SEMUA TASK SELESAI" di atas) — Task 3:
  Baris Task 3 di atas ("Coredump kosong TAPI partisinya lahir di be760d3, sesudah kejadian
  kemarin -> tidak membebaskan apa pun") DIDASARI PREMIS YANG SALAH, dibiarkan apa adanya
  di atas untuk jejak, dikoreksi di sini. Premisnya adalah bahwa partisi coredump BARU
  muncul di be760d3 (Task 1). Itu keliru: dibandingkan byte-per-byte dengan
  partitions/default.csv bawaan toolchain (framework-arduinoespressif32), partisi
  coredump SUDAH ADA di offset dan ukuran yang identik (0x3F0000, 0x10000) SEBELUM
  be760d3 — yang berubah di be760d3 hanya app0/app1/spiffs, bukan coredump (juga bukan
  nvs/otadata, yang justru semula disangka pindah offset; lihat koreksi §3.1 di spec).
  Akibatnya kesimpulan Task 3 terbalik: coredump kosong BUKAN karena partisinya belum ada
  saat kejadian 13 Agustus — partisi itu sudah ada. Yang sebenarnya terjadi: KALAU kejadian
  13 Agustus itu panic, coredump-nya kemungkinan sudah tersimpan di 0x3F0000, lalu
  `pio run -t erase` di Task 1 (be760d3) MENGHAPUSNYA sebelum Task 3 sempat membaca. Jadi
  coredump kosong bisa jadi bukti hilang karena langkah kita sendiri, bukan bukti bahwa
  tidak pernah ada coredump untuk dibaca. Akar penyebab reboot 13 Agustus tetap TIDAK
  DIKETAHUI — sekarang dengan alasan yang benar. Detail lengkap: spec
  docs/superpowers/specs/2026-08-13-fondasi-paritas-design.md §3.1 dan §7 (blok "KOREKSI").
  Tidak ada perbaikan kode untuk temuan ini — murni koreksi catatan.

REVIEW AKHIR SELURUH BRANCH (opus, 18 commit 6a38f24..e5e175c) — 2 Important lintas-task:
  #1 doOnOff: loop retry 20x tidak keluar saat MB_TIMEOUT/CRC/MALFORMED. Cacat LAMA,
     tapi Task 9 melipatgandakan kedua ujungnya (jendela comm_lost 3x lebih lebar +
     mb_mtx dipegang lebih lama) -> satu enable ke BESS mati bisa membungkam task_cmd
     ~176 dtk dan menggerus jaminan Task 7. DIPERBAIKI 80503a1, terukur 3,05 dtk.
  #2 Premis erase penuh SALAH: nvs/otadata/coredump identik dengan default.csv toolchain.
     Konsekuensi: coredump dari kejadian 13 Agu (kalau panic) DIHAPUS erase Task 1
     sebelum sempat dibaca. Penjelasan Task 3 di atas = alasan salah untuk pengamatan
     benar. Dikoreksi di spec §3.1/§7 + append di bawah (baris asli sengaja dibiarkan).
  Fix wave 80503a1 + re-review: 3/3 ADDRESSED, no new breakage, LOLOS MERGE.
  Backlog naik ke user: truncation 511 B (BLOCKER untuk sub-proyek G/OTA), heap tidak
  di telemetri, TASK_WDT tak pernah bisa muncul, waitStatusBit timeout dilaporkan
  rejected, mqttEnqueueTelemetry tanpa cek n, connected bukan volatile.

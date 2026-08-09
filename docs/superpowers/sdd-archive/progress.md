# SDD ledger — plan: docs/superpowers/plans/2026-08-09-gateway-bess.md
Task 1: complete (commits e167937..0b1dc46, review clean)
Task 2: fix round 1/5 (2 addressed, 0 open — write_block atomik + test; commits 2f95650..c4763fd)
Task 2: minor (deferred): get_signed() memaksa two's-complement tanpa cek RegDef.signed — jebakan laten bila dipanggil pada register unsigned bernilai >=0x8000
Task 2: complete (commits f88c576..c4763fd, review clean)
Task 3: minor (deferred): busy vs malformed ordering di jalur tulis (03 sebelum 06) — ambigu spec, dari kode brief; + tak ada test frame <4 byte -> diam
Task 3: complete (commits c4763fd..f3c55b6, review clean)
Task 4: minor (deferred): wh_charge/wh_discharge tanpa test; test grid terlalu longgar; ramp-down not-running tak teruji dengan rate kecil
Task 4: complete (commits f3c55b6..664593a, review clean)
Task 5: fix round 1/5 (2 addressed, 0 open — status_word off-by-one transisi + test via register 2057; commits 2bc5879..bb1100f)
Task 5: minor (deferred): bit4 off_grid 2057 tidak pernah di-set (selalu on-grid; scope-narrowing sadar); beberapa test masih assert sim.sm.state sebagai auxiliary
Task 5: complete (commits 664593a..bb1100f, review clean)
Task 6: minor (deferred): narasi laporan implementer salah hitung (61 vs 85 alarm) — kode benar; koreksi ditambahkan ke task-6-report.md
Task 6: complete (commits bb1100f..f7605bb, review clean)
Task 7: minor (deferred): loop run() busy-poll tanpa throttle (dari kode brief) — perhatikan CPU bila jadi masalah di bench
Task 7: complete (commits f7605bb..87a3fd3, review clean)
Task 8: minor (deferred): parameter mati expect_silence di master_probe.xfer (dari kode brief) — nit kosmetik
Task 8: complete (commits 87a3fd3..0c6d1e8, review clean)
Task 9: fix round 1/5 (lokasi test; salah diagnosis per-suite) + round 2/5 (akar: folder wajib prefix test_; commits c4fbde8..ed8703b)
Task 9: complete (commits 0c6d1e8..ed8703b, review clean). CATATAN dispatch berikut: suite native pakai test/test_native_<nama>/main.cpp (prefix test_ wajib)
Task 10: complete (commits ed8703b..609756d, review clean)
Task 11: complete (commits 609756d..55e455c, review clean; paritas nama alarm 85+14 vs alarms.py diverifikasi penuh)
Task 10 (post-hoc): fix stub setUp/tearDown test_native_frame (commit f455d2d) — regresi link terdeteksi saat review Task 11; 12/12 native hijau (diverifikasi controller)
Task 12: fix round 1/5 (2 addressed, 0 open — truncation contract buildTelemetryJson + test; commits 9e33b38..5584288)
Task 12: complete (commits f455d2d..5584288, review clean; payload nyata ~3.3 KB)
Task 13: complete (commits 5584288..fe5d0b3, review clean; smoke HW: boot + [wifi] OK rssi=-42..-53 ip=192.168.18.52; WiFi bench = "Lantai 2")
Task 13: minor (deferred): race wifiInit/wifiTick -> WiFi.begin dobel tiap boot (buang ~4 dtk); next_try_ms tak direset saat connect (retry pertama pasca-putus bisa tertunda) — dari kode brief; kandidat fix saat Task 15 menyentuh area ini
Task 14: complete (commits fe5d0b3..c800276, review clean; bench: poll RS485 nyata OK, soc/vdc cocok simulator, COMM_LOST + recovery terverifikasi)
Task 14: minor (deferred): Serial.printf di dalam stateLock (task_bess log 5 dtk) — salin lokal dulu; xfer tak menandai RX overflow (tak reachable saat ini)
Task 15: complete (commits c800276..8d04fc1, review clean; e2e 6/6 butir lulus di bench: telemetri bess, enable->RUN, set_power 5kW soc turun, bad_value, disable, comm_lost roundtrip)
Task 15: minor (deferred): formula power_w redundan /10*10; queue penuh drop command diam-diam (tanpa ack); MQTT connect pertama selalu gagal DNS pra-WiFi (auto-recover); 2057 hardcode di waitStatusBit; flash 84.3% (perhatian utk TLS nanti)
Task 16: complete (commits 8d04fc1..84704ca, review clean; verifikasi penuh 57 pytest + selftest + 19 native + build; uji dingin bench lulus; 3 README)
Task 16: minor (deferred): README firmware sebut payload ±2-3 KB, terukur ~3,3 KB — longgarkan ke ~3-4 KB
FINAL REVIEW (fable): MERGEABLE — 0 Critical/Important; fix wave docs 1d4307a (payload ~3-4 KB + bad_value) ADDRESSED via scoped re-review. Seluruh deferred ditriase OK-TO-DEFER (lihat verdict final). Backlog bernilai: queue penuh drop command tanpa ack; wifi rollover+dobel begin; log exc Modbus; flash 84.3% sebelum TLS.

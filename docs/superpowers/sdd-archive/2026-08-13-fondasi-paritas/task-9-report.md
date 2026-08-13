# Task 9 Report: Catat kode exception Modbus per blok

**Status:** DONE

**Commit:** `b65500f` — feat(fw): catat kode exception Modbus per blok register

## Verifikasi Bench

### Simulasi Kegagalan (Simulator dimatikan)

Keempat blok dicatat sebelum COMM_LOST terdeteksi:

```
[bess] blok telem: gagal (status 1)
[bess] blok alarm: gagal (status 1)
[bess] blok pset: gagal (status 1)
[bess] blok param: gagal (status 1)
```

Lalu log standar COMM_LOST:
```
[bess] COMM_LOST p=0.0kW soc=60.0% vdc=826.6V status=0x8B00
```

### Pemulihan (Simulator dihidupkan kembali)

Polling berhasil tanpa reboot:
```
[bess] OK p=0.0kW soc=60.0% vdc=826.6V status=0x8B00
```

## Detail Teknis

- **Bentuk error:** `gagal (status 1)` bukan exception code. Status 1 = timeout Modbus (simulasi kegagalan menghentikan perangkat serial), bukan exception protokol. Ini perilaku yang diharapkan.
- **Perilaku short-circuit:** Dikonfirmasi **dihilangkan** — keempat blok selalu dibaca walau ada kegagalan pertama. Jika hanya satu blok dicatat, rantai `&&` masih aktif (tapi output di atas menunjukkan semua empat).
- **Tanpa reboot:** Gateway tetap hidup (hanya comm_lost flag yang naik), tidak ada restart MCU.

## Validasi Native Tests

```
================= 36 test cases: 36 succeeded in 00:00:05.439 =================
```

Semua 36 test lolos, tidak ada degradasi.

## Perubahan Perilaku Disengaja

Sebelumnya: `ok = blok1 && blok2 && blok3 && blok4` — loop berhenti di gagal pertama, tiga blok tidak pernah dicoba.

Sekarang: Keempat blok **selalu** dibaca; hanya jika **semua OK** maka decode data. Diagnosis lengkap dengan biaya waktu bus minimal saat BESS mati.

## Kesimpulan

✓ Kode exception/status per blok ditampilkan
✓ Short-circuit dihilangkan
✓ Pembacaan keempat blok terjadi independen
✓ Pemulihan otomatis tanpa reboot
✓ Native test suite tetap 36/36 hijau

#ifndef ACK_RING_H
#define ACK_RING_H

#include <stddef.h>

// ack_ring -- logika MURNI sub-proyek H: ring buffer 8 ack terakhir yang
// dibaca GET /api/acks (dashboard lokal). Entrinya adalah teks JSON APA
// ADANYA yang sudah dihasilkan buildAckJson/schedBuildAck/otaBuildAck --
// file ini TIDAK pernah mem-parse ulang, hanya menyimpan + menggabungkan
// jadi satu array JSON `[terbaru, ..., terlama]`.
//
// Dipakai dari src/task_cmd.cpp: satu AckRing + mutex FreeRTOS pendek
// (push saat sendAck/sendScheduleAck, snapshot disalin di bawah lock lalu
// dibangun jadi JSON DI LUAR lock -- lihat taskCmdGetAcksJson).

#define ACK_RING_CAP        8
// == ACK_JSON_MAX (src/config.h). Disalin sebagai konstanta independen
// karena file ini dipakai di native test tanpa config.h (khusus ESP) --
// buildAckJson/schedBuildAck tidak pernah menulis lebih dari ACK_JSON_MAX,
// jadi entri di sini tidak pernah terpotong dalam pemakaian nyata; kalaupun
// suatu saat tak sinkron, ackRingPush memotong dengan aman (lihat di bawah).
#define ACK_RING_ENTRY_MAX  512
// Cukup untuk ACK_RING_CAP entri (masing-masing <= ACK_RING_ENTRY_MAX byte
// + koma) dibungkus `[` `]`.
#define ACK_RING_JSON_CAP   (ACK_RING_CAP * (ACK_RING_ENTRY_MAX + 1) + 8)

struct AckRing {
    char entries[ACK_RING_CAP][ACK_RING_ENTRY_MAX];
    size_t lens[ACK_RING_CAP];
    int count;   // jumlah entri terisi (<= ACK_RING_CAP)
    int next;    // indeks slot berikutnya yang akan ditulis (wrap around)
};

void ackRingInit(AckRing& r);

// Tambahkan satu ack JSON (n byte, TANPA perlu NUL di akhir) ke ring --
// entri PALING BARU. Kalau n >= ACK_RING_ENTRY_MAX, entri dipotong ke
// ACK_RING_ENTRY_MAX-1 byte (defensif -- lihat komentar ACK_RING_ENTRY_MAX
// di atas soal kenapa ini seharusnya tidak pernah terjadi).
void ackRingPush(AckRing& r, const char* json, size_t n);

// Bangun `[ack_terbaru, ..., ack_terlama]` (maks ACK_RING_CAP elemen) dengan
// menggabungkan entri APA ADANYA -- tanpa re-parse/validasi (sudah JSON valid
// dari builder command). Return 0 kalau tidak muat di `cap` (out tidak
// disentuh sama sekali dalam kasus itu -- caller boleh cek n==0 lalu jatuh
// ke fallback "[]").
size_t ackRingBuildJson(const AckRing& r, char* out, size_t cap);

#endif

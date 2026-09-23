#ifndef WEB_CMD_H
#define WEB_CMD_H

#include <stddef.h>

// web_cmd -- logika MURNI sub-proyek H untuk POST /api/command. Command dari
// dashboard/API lokal harus punya "id" seperti command MQTT (buildAckJson
// selalu menyalin `c.id` apa adanya ke ack) -- kalau operator/browser tidak
// mengirimnya (atau mengirim string kosong), gateway membangkitkan
// "web-<millis>" supaya ack tetap bisa dikorelasikan lewat GET /api/acks.
// `millis_now` disuntikkan (bukan Arduino millis() langsung) supaya
// testable native.
//
// Return false kalau body bukan objek JSON valid -- caller (src/web.cpp)
// meneruskan body ASLI apa adanya ke taskCmdSubmitWeb; task_cmd yang akan
// menolaknya "bad_json"/"unsupported_cmd" via parseCommand, sama seperti
// command MQTT yang korup (satu kontrak kegagalan, bukan dua). `out`/`out_len`
// dan `id_out` TIDAK disentuh saat return false.
//
// Saat return true: `out` (NUL-terminated, `out_len` byte tanpa NUL) berisi
// JSON yang PASTI punya field "id" tak kosong; `id_out` (NUL-terminated)
// berisi id yang dipakai (yang sudah ada di body, atau yang baru
// dibangkitkan) untuk balasan HTTP 202 `{"queued":true,"id":...}`.
bool webCmdEnsureId(const char* json, size_t len, unsigned long millis_now,
                     char* out, size_t out_cap, size_t& out_len,
                     char* id_out, size_t id_cap);

#endif

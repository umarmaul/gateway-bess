#include "ack_ring.h"
#include <string.h>

void ackRingInit(AckRing& r) {
    r.count = 0;
    r.next = 0;
    for (int i = 0; i < ACK_RING_CAP; i++) {
        r.entries[i][0] = 0;
        r.lens[i] = 0;
    }
}

void ackRingPush(AckRing& r, const char* json, size_t n) {
    if (n >= ACK_RING_ENTRY_MAX) n = ACK_RING_ENTRY_MAX - 1;
    int slot = r.next;
    memcpy(r.entries[slot], json, n);
    r.entries[slot][n] = 0;
    r.lens[slot] = n;
    r.next = (r.next + 1) % ACK_RING_CAP;
    if (r.count < ACK_RING_CAP) r.count++;
}

size_t ackRingBuildJson(const AckRing& r, char* out, size_t cap) {
    if (cap < 2) return 0;
    size_t pos = 0;
    out[pos++] = '[';
    // Entri terbaru dulu: mulai dari (next-1), mundur sebanyak `count` kali.
    for (int i = 0; i < r.count; i++) {
        int slot = (r.next - 1 - i + ACK_RING_CAP) % ACK_RING_CAP;
        size_t n = r.lens[slot];
        size_t needed = n + (i > 0 ? 1 : 0);   // +1 untuk koma pemisah
        if (pos + needed + 1 > cap) return 0;  // +1 untuk ']' penutup
        if (i > 0) out[pos++] = ',';
        memcpy(out + pos, r.entries[slot], n);
        pos += n;
    }
    if (pos + 2 > cap) return 0;   // ']' + NUL
    out[pos++] = ']';
    out[pos] = 0;
    return pos;
}

#include "web_cmd.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>

bool webCmdEnsureId(const char* json, size_t len, unsigned long millis_now,
                     char* out, size_t out_cap, size_t& out_len,
                     char* id_out, size_t id_cap) {
    JsonDocument doc;
    if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
    if (!doc.is<JsonObject>()) return false;
    JsonObject obj = doc.as<JsonObject>();

    const char* existing = obj["id"] | "";
    if (existing[0] == 0) {
        char gen[32];
        snprintf(gen, sizeof(gen), "web-%lu", millis_now);
        obj["id"] = gen;
    }

    const char* final_id = obj["id"] | "";
    if (id_cap) {
        strncpy(id_out, final_id, id_cap - 1);
        id_out[id_cap - 1] = 0;
    }

    size_t need = measureJson(obj);
    if (need + 1 > out_cap) return false;   // +1 utk NUL -- tak muat, caller pakai body asli
    out_len = serializeJson(obj, out, out_cap);
    return out_len > 0;
}

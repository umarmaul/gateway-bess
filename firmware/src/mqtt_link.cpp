#include "mqtt_link.h"
#include <Arduino.h>
#include <mqtt_client.h>
#include "config.h"
#include "secrets.h"
#include "task_cmd.h"

static esp_mqtt_client_handle_t cli = nullptr;
static bool connected = false;
static char t_telemetry[48], t_status[48], t_command[48], t_ack[52];

static void onEvent(void*, esp_event_base_t, int32_t event_id, void* event_data) {
    auto* e = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            connected = true;
            esp_mqtt_client_publish(cli, t_status, "online", 0, 1, 1);
            esp_mqtt_client_subscribe(cli, t_command, 1);
            Serial.println("[mqtt] connected");
            break;
        case MQTT_EVENT_DISCONNECTED:
            connected = false;
            Serial.println("[mqtt] disconnected");
            break;
        case MQTT_EVENT_DATA:
            if (e->topic_len == strlen(t_command) &&
                !strncmp(e->topic, t_command, e->topic_len))
                taskCmdSubmit(e->data, e->data_len);
            break;
        default: break;
    }
}

void mqttInit(const char* gw) {
    snprintf(t_telemetry, sizeof(t_telemetry), "device/%s/telemetry", gw);
    snprintf(t_status, sizeof(t_status), "device/%s/status", gw);
    snprintf(t_command, sizeof(t_command), "device/%s/command", gw);
    snprintf(t_ack, sizeof(t_ack), "device/%s/command/ack", gw);
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
    cfg.session.last_will.topic = t_status;
    cfg.session.last_will.msg = "offline";
    cfg.session.last_will.qos = 1;
    cfg.session.last_will.retain = 1;
    cli = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(cli, MQTT_EVENT_ANY, onEvent, nullptr);
    esp_mqtt_client_start(cli);
}

bool mqttConnected() { return connected; }

bool mqttEnqueueTelemetry(const char* json, size_t n) {
    if (!cli) return false;
    return esp_mqtt_client_enqueue(cli, t_telemetry, json, n, 1, 0, true) >= 0;
}

bool mqttPublishAck(const char* json, size_t n) {
    if (!cli || !connected) return false;
    return esp_mqtt_client_publish(cli, t_ack, json, n, 1, 0) >= 0;
}

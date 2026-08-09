#pragma once
// Pin — fakta hardware dari BEPESP32_WiFi_Extension/src/Config.h
#define PIN_BESS_RX        21
#define PIN_BESS_TX        20
#define PIN_BESS_REDE      22
#define PIN_LED_BESS       18
#define PIN_LED_WIFI       14
// Modbus BESS (BSL AC series V2.1.0)
#define BESS_BAUD          9600
#define BESS_NODE          1
#define MB_FRAME_GAP_MS    105     // spec: >= 100 ms
#define MB_TIMEOUT_MS      500
#define MB_RETRIES         2       // total 3 percobaan
#define POLL_PERIOD_MS     1500
#define COMM_LOST_AFTER    3       // siklus gagal beruntun
// Register kunci
#define REG_TELEM_START    1050
#define REG_TELEM_COUNT    59
#define REG_ALARM_START    2050
#define REG_ALARM_COUNT    8
#define REG_P_SET          3050
#define REG_PARAM_START    3146
#define REG_PARAM_COUNT    39      // 3146..3184 (rated .. soc)
#define REG_ONOFF          5050
// MQTT
#define MQTT_KEEPALIVE_S       300
#define MQTT_NETWORK_TIMEOUT_MS 60000
#define TELEMETRY_PERIOD_MS    60000
#define FW_VERSION         "bess-0.1.0"

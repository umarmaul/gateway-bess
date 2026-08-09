#pragma once
#include <Arduino.h>
#include "bess_data.h"

struct AppState {
    BessData bess;
    uint32_t seq = 0;
    SemaphoreHandle_t mtx = nullptr;
};
extern AppState g_state;
void stateInit();
void stateLock();
void stateUnlock();

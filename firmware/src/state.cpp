#include "state.h"
AppState g_state;
void stateInit() { g_state.mtx = xSemaphoreCreateMutex(); }
void stateLock() { xSemaphoreTake(g_state.mtx, portMAX_DELAY); }
void stateUnlock() { xSemaphoreGive(g_state.mtx); }

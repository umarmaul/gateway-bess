#pragma once
#include <Arduino.h>

void wifiInit();
void wifiTick();
bool wifiConnected();
void wifiGw(char out[13]);

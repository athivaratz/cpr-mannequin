#ifndef CPR_ESP32_BLE_H
#define CPR_ESP32_BLE_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace ble {

void begin(const char* deviceName, QueueHandle_t cmdQueue);
void loop();
void setConfigLine(const char* line);
bool notifyLine(const char* line);
bool isConnected();
bool isSubscribed();
void pauseAdvertising();
void resumeAdvertising();

} // namespace ble

#endif

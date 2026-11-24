#ifndef CPR_ESP32_NET_H
#define CPR_ESP32_NET_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace net {

void begin(QueueHandle_t outQueue, QueueHandle_t cmdQueue,
           const char* wifiPassword);
void loop();
void setConfigLine(const char* line);

} // namespace net

#endif

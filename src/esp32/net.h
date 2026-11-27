#ifndef CPR_ESP32_NET_H
#define CPR_ESP32_NET_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace net {

void begin(QueueHandle_t outQueue, QueueHandle_t cmdQueue);
void loop();
void setConfigLine(const char* line);
void setFrame(const char* line);
void setDiagSensor(uint8_t sps, bool hxPresent, long rawAdc, long offset,
                   float scale, uint16_t rebootCount, uint8_t hxStatus);
void noteQueueDrop();
bool serialStreaming();
void setSerialStreaming(bool on);

} // namespace net

#endif

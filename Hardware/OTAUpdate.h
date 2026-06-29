#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <Arduino.h>
#include "../secrets.h"   // OTA 烧录口令（已在 .gitignore，见 secrets.h.example）

// ====================== ArduinoOTA 无线烧录 ======================
// 一次 USB 烧录带 OTA 的固件后，之后改完代码可直接通过 WiFi 推新固件，无需插线。
// 设备会以 OTA_HOSTNAME.local 出现在 Arduino IDE 的「网络端口」里；
// 也可用 espota.py / arduino-cli 走网络协议上传。
#define OTA_HOSTNAME "smart-bag-esp32"
#define OTA_PASSWORD SECRET_OTA_PASSWORD   // OTA 烧录口令；置空字符串则不校验

void ota_setup();    // 初始化 OTA（须在 WiFi 连上后调用）
void ota_handle();   // 处理 OTA 请求（须在 loop 中反复调用）

#endif

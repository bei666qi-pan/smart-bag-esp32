#include "Hardware/OTAUpdate.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <cstring>
#include "Hardware/RemoteDebug.h"

void ota_setup() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  if (strlen(OTA_PASSWORD) > 0) {
    ArduinoOTA.setPassword(OTA_PASSWORD);
  }

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "固件" : "文件系统";
    LOGln("[OTA] 开始无线更新: " + type);
  });
  ArduinoOTA.onEnd([]() {
    LOGln("[OTA] 更新完成，设备即将重启");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    LOGf("[OTA] 进度 %u%%\r", total ? (progress * 100) / total : 0);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    LOGf("[OTA] 错误码 %u\n", error);
  });

  ArduinoOTA.begin();
  LOGf("[OTA] 就绪: 主机名 %s.local, IP %s, 口令保护=%s\n",
       OTA_HOSTNAME, WiFi.localIP().toString().c_str(),
       (strlen(OTA_PASSWORD) > 0) ? "是" : "否");
}

void ota_handle() {
  ArduinoOTA.handle();
}

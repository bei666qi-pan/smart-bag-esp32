#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_camera.h>
#include "MQTT.h"
#include "Hardware/AHT10.cpp"
#include "Hardware/AHT10.h"
#include "Hardware/NEO_M9N.cpp"
#include "Hardware/NEO_M9N.h"
#include "Hardware/LED.cpp"
#include "Hardware/LED.h"
#include "Hardware/camera.cpp"
#include "Hardware/camera.h"
#include "Hardware/Screen.cpp"
#include "Hardware/Screen.h"
#include "Hardware/RemoteDebug.cpp"
#include "Hardware/RemoteDebug.h"
#include "Hardware/OTAUpdate.cpp"
#include "Hardware/OTAUpdate.h"

float temp  = 0.0;   // 温度
float humid = 0.0;   // 湿度
float lat   = 0.0;   // 纬度
float lng   = 0.0;   // 经度
unsigned long lastReportTime = 0;
bool remoteReady = false;   // OTA + telnet 远程服务是否已就绪

// WiFi 就绪后，懒初始化一次远程联调服务（OTA 无线烧录 + telnet 远程日志）
void ensureRemoteServices() {
  if (!remoteReady && WiFi.status() == WL_CONNECTED) {
    rdbg_setup();   // telnet 远程日志
    ota_setup();    // ArduinoOTA 无线烧录
    remoteReady = true;
    LOGln("=== 远程联调服务已就绪（OTA + telnet）===");
  }
}

// ==================== 主初始化 ====================
void setup() {
  Serial.begin(115200);
  Serial.println("=== 智能书包系统启动 ===");

  led_init(255);
  Serial.println("氛围灯 初始化完成");

  aht10_setup();
  Serial.println("AHT10 初始化完成");

  NEO_M9N_Init();
  Serial.println("NEO_M9N 初始化完成");

  MQTT_set();   // 内部连接 WiFi
  Serial.println("MQTT 初始化完成");

  // NTP 校时（东八区）：供屏幕实时时钟显示；WiFi 已在 MQTT_set 中连接
  configTime(8 * 3600, 0, "ntp.aliyun.com", "ntp.tencent.com", "pool.ntp.org");

  camera_system_init();   // 顺序不能调换：依赖 MQTT 已连接 WiFi
  Serial.println("摄像头 初始化完成");

  Screen_setup();   // 修复：此前误写成函数声明 "void Screen_setup();"，屏幕从未真正初始化
  Serial.println("屏幕 初始化完成");

  // 远程联调服务：WiFi 已连上则立即就绪；未连上则在 loop 中 WiFi 起来后懒初始化
  ensureRemoteServices();
}

// ==================== 主循环 ====================
void loop() {
  // 远程服务：WiFi 起来后懒初始化一次，然后每轮维护（OTA 必须高频 handle）
  ensureRemoteServices();
  if (remoteReady) {
    ota_handle();     // 处理无线烧录请求
    rdbg_handle();    // 维护 telnet 客户端
  }

  // MQTT 必须实时运行（维持连接、处理下行命令）
  mqtt_loop();

  // 屏幕按钮事件必须实时处理（开灯/关灯/拍照）
  handleScreenEvents();

  // [就近告警] 麦克风故障时 LED 红闪（非阻塞，~500ms 翻转）：书包旁的人无需看屏/登录网页即可察觉，
  // 知道该去拔插 USB 麦克风。标志由 v5/bag/voice/health 的 need_physical_replug 置位。
  static bool micBlinkOn = false;
  static unsigned long micBlinkLast = 0;
  if (g_micAlert && millis() - micBlinkLast >= 500) {
    micBlinkLast = millis();
    micBlinkOn = !micBlinkOn;
    if (micBlinkOn) led_on(255, 0, 0); else led_all_off();
  }

  // [异常可见性] WiFi 状态变化即时上屏（不再等 10s 上报周期），断网后用户很快能从屏幕看出离线。
  static bool lastWifiUp = false, wifiInit = false;
  bool wifiUp = (WiFi.status() == WL_CONNECTED);
  if (!wifiInit || wifiUp != lastWifiUp) {
    wifiInit = true; lastWifiUp = wifiUp;
    updateWifiStatus(wifiUp);
  }

  // 定时上报逻辑
  if (millis() - lastReportTime >= (unsigned long)INTERVAL * 1000UL) {
    lastReportTime = millis();

    if (aht10_read(temp, humid)) {
      // 过滤异常值：温度应在 [-40, 85]，湿度应在 [0, 100]，
      // 任一越界即视为异常，输出 9999 作为哨兵值。
      if (temp < -40.0 || temp > 85.0 || humid < 0.0 || humid > 100.0) {
        temp  = 9999.0;
        humid = 9999.0;
      }
    } else {
      // 读取失败也输出哨兵值，避免上报上一帧的陈旧数据
      temp  = 9999.0;
      humid = 9999.0;
    }

    NEO_M9N_Update();
    NEO_M9N_ReadLocation(lat, lng);   // 数据异常时经纬度输出 0.0

    // 上报 + 上屏
    mqtt_publish_sensors(temp, humid);
    Screen_readSensorData(temp, humid);

    mqtt_publish_gps(lat, lng);
    updateGps(lat, lng);
    updateWifiStatus(WiFi.status() == WL_CONNECTED);   // 刷新屏幕联网状态(t_wifi)
    feedWaveform(temp);                                 // 大蓝格实时温度曲线

    // 把本轮上报镜像到远程日志，方便远程全链路联调观察
    LOGf("[REPORT] temp=%.1f humid=%.1f lat=%.6f lng=%.6f\n", temp, humid, lat, lng);

    // 周期性抓拍上传（受 camera 模块 uploadInterval 限速）
    camera_system_loop();
  }

  // 轻微延时，不影响 MQTT 与屏幕事件响应
  delay(50);
}

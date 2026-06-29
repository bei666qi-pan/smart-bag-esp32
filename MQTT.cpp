#include <WiFi.h>
#include <PubSubClient.h>
#include "MQTT.h"
#include "Hardware/LED.h"
#include "Hardware/Screen.h"       // 下行命令需要驱动屏幕（screen_text / AI 回复上屏）
#include "Hardware/RemoteDebug.h"  // 把命令/ACK/连接日志镜像到 telnet 远程联调
#include "secrets.h"               // WiFi / MQTT 凭据（已在 .gitignore，见 secrets.h.example）

// ==================== 配置项 ====================
const char* WIFI_SSID     = SECRET_WIFI_SSID;
const char* WIFI_PASS     = SECRET_WIFI_PASS;
const char* MQTT_HOST     = "mqtt.bag.versecraft.cn";
const int   MQTT_PORT     = 1883;
const char* MQTT_CLIENTID = "ESP32_S3_BAG_001"; // 唯一ID
// broker 已开启鉴权（2026-06-15）：匿名可连可订阅，但匿名"写入"被静默丢弃→遥测上不了云。
// ESP32 主板专用账号 bag01：可写 sensors/status/gps。不加这个，传感器数据进不了云端。
const char* MQTT_USER     = SECRET_MQTT_USER;
const char* MQTT_PASS     = SECRET_MQTT_PASS;

// =================================================

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastSend = 0;

// ==================== MQTT心跳包 ====================
void mqtt_loop(){
  if (!client.connected()) {
    // 非阻塞：断开时每 3s 尝试一次单次重连，绝不在此死循环冻结 telnet/OTA/屏幕/传感器
    static unsigned long lastTry = 0;
    if (millis() - lastTry >= 3000) {
      lastTry = millis();
      mqtt_reconnect();
    }
    return;
  }
  client.loop();
}

// ==================== MQTT 回调（接收命令） ====================
void callback(char* topic, byte* payload, unsigned int len) {
  char buf[512];
  if(len >= 512) len = 511;
  memcpy(buf, payload, len);
  buf[len] = '\0';

  // 打印原始数据（必看，现在绝对不会乱码）
  Serial.println("===== 原始报文 =====");
  Serial.println(buf);

  // [就近告警] 语音健康主题：payload 形如 {"status":"need_physical_replug"|"ok"|"recovered",...}，
  // 无 action/id，单独分支处理——故障即在本机屏幕红字 + LED 红闪现场提示，不回 ACK。
  if (strcmp(topic, TOPIC_VOICE_HEALTH) == 0) {
    String hs = "";
    char* p_st = strstr(buf, "\"status\"");
    if (p_st) {
      p_st = strstr(p_st, ":");
      while (*p_st == ':' || *p_st == ' ' || *p_st == '"') p_st++;
      char* e_st = strstr(p_st, "\"");
      if (e_st) { *e_st = '\0'; hs = p_st; *e_st = '"'; }
    }
    hs.trim();
    LOGf("[VOICE_HEALTH] status=%s\n", hs.c_str());
    if (hs == "need_physical_replug")      screen_set_mic_alert(true);
    else if (hs == "ok" || hs == "recovered") screen_set_mic_alert(false);
    return;
  }

  // 解析 cmd id,action 和 value
  String id = "";
  String action = "";
  String value = "";

  // 提取id
  char* p = strstr(buf, "\"id\"");
  if (p) {
    p = strstr(p, ":");                                       //找到冒号
    while (*p == ':' || *p == ' ' || *p == '"') p++;  //跳过所有符号空格
    char* e = strstr(p, "\"");
      if (e) {
        *e = '\0';        //临时截断
        id = p;
        *e = '"';         //立刻恢复引号！数据保持完整
      }
  }

  // 提取 action
char* p_act = strstr(buf, "\"action\"");
if (p_act) {
  p_act = strstr(p_act, ":");                                       //找到冒号
  while (*p_act == ':' || *p_act == ' ' || *p_act == '"') p_act++;  //跳过所有符号空格
  char* e_act = strstr(p_act, "\"");
    if (e_act) {
      *e_act = '\0';
      action = p_act;
      *e_act = '"'; 
    }
}
// 提取 value

char* p_val = strstr(buf, "\"value\"");   // 仅锚定键名；冒号后允许空格（兼容 Jetson json.dumps 的 "value": "x"）
if (p_val) {
  p_val = strstr(p_val, ":");
  while (*p_val == ':' || *p_val == ' ' || *p_val == '"') p_val++;
  char* e_val = strstr(p_val, "\"");
    if (e_val) {
      *e_val = '\0';
      value = p_val;
      *e_val = '"'; 
    }
}

// ==================== 强制清理字符串（关键）====================
  action.trim();
  value.trim();
  id.trim();
 
  // ==================== 调试串口====================
  Serial.println("==================================");
  Serial.print("收到 action = ["); Serial.print(action); Serial.println("]");
  Serial.print("收到 value  = ["); Serial.print(value); Serial.println("]");
  Serial.print("收到 id     = ["); Serial.print(id); Serial.println("]");
  // 镜像到远程日志（telnet）
  LOGf("[CMD] action=%s value=%s id=%s\n", action.c_str(), value.c_str(), id.c_str());

  // 分支处理不同指令
  int   ack_status = 0;
  const char* ack_msg = "OK";

  if (action == "screen_text") {
    // 下发家长文字到屏幕（同时写入用户文字区与 AI 回复区，确保可见）
    Serial.print("屏幕显示文字：");
    Serial.println(value);
    updateWritingText(value);
    updateAIResponse(value);
  }
  else if (action == "mode_switch") {
    // 切换工作模式
    if (value == "focus_mode") {
      Serial.println("切换为 专注模式");
      led_all_off();
      led_breathing(0, 0, 255);
      updateWritingText("专注模式");
    }
    else if (value == "normal_mode") {
      Serial.println("切换为 普通模式");
      led_all_off();
      led_breathing(0, 255, 0);
      updateWritingText("普通模式");
    }
    else {
      Serial.println("未知的 mode_switch 取值");
      ack_status = 1;
      ack_msg = "unknown mode";
    }
  }
  else if (action == "indicator") {
    // [语音联动] 小乐唤醒/思考/休眠指示灯（来自 v5/bag/voice/cmd，payload 无 id 故不回 ACK）
    // 同步上屏：LED 在日光/包内可能看不清，屏幕 t6 同时显示「在听中/思考中/待命中」。
    // 麦克风告警期间不抢占 LED（红闪优先），但语音状态仍可上屏。
    if (value == "listening")      { Serial.println("指示灯：聆听(青)");   if (!g_micAlert) led_on(0, 255, 255); }
    else if (value == "thinking")  { Serial.println("指示灯：思考(琥珀)"); if (!g_micAlert) led_on(255, 150, 0); }
    else if (value == "idle")      { Serial.println("指示灯：休眠(灭)");   if (!g_micAlert) led_all_off(); }
    else { ack_status = 1; ack_msg = "unknown indicator"; }
    screen_set_voice_state(value);
  }
  else if (action == "set_timetable") {
    // [课表下行] 网页/MQTT 动态更新今日课表（value=GBK名hex,时间;...）。这是产品级缺口的补全：
    // 此前课表写死在固件里、谁都改不了；现在家长在网页改完即可下发到书包屏幕。
    Serial.print("更新课表：");
    Serial.println(value);
    setTimetableFromPayload(value);
  }
  else if (action == "screen_probe") {
    // 逆向探针：给 t0.txt..t29.txt 写 T0..T29，真机拍照定位控件号
    Serial.println("屏幕探针：t0..t29");
    screen_probe();
  }
  else if (action == "screen_raw") {
    // 经 MQTT 透传任意 TJC 指令（dim/page/控件名探测），value 即原始指令
    screen_raw(value);
  }
  else {
    // 未识别的 action，回执非 0 状态，便于网页端判断命令失败
    Serial.print("未识别的 action: ");
    Serial.println(action);
    ack_status = 1;
    ack_msg = "unknown action";
  }

  // 回复 ACK：字段必须叫 cmd_id（网页端用它匹配 pendingCmd）
  if (id != "") {
    char ack[256];
    sprintf(ack, "{\"cmd_id\":\"%s\",\"status\":%d,\"msg\":\"%s\"}",
            id.c_str(), ack_status, ack_msg);
    client.publish(TOPIC_CMD_ACK, ack);
    LOGf("[ACK] %s\n", ack);
  }
}

// ==================== MQTT 重连（带LWT） ====================
void mqtt_reconnect() {
  // 单次连接尝试（非阻塞，无 while/无 delay）；失败立即返回，由 mqtt_loop 控制 3s 重试节奏。
  // LWT willRetain=1：断开后 broker 保留 offline；在线时再发 retained online。
  // 关键：状态必须 retained，否则晚连接的网页/浏览器收不到→ 误判设备离线/掉线。
  if (client.connect(MQTT_CLIENTID, MQTT_USER, MQTT_PASS, TOPIC_STATUS, 0, 1, "{\"status\":\"offline\"}")) {
    client.subscribe(TOPIC_CMD);
    client.subscribe(TOPIC_VOICE_CMD);    // [语音联动] 同时订阅 Jetson 语音控制下行（无 id，不回 ACK）
    client.subscribe(TOPIC_VOICE_HEALTH); // [就近告警] 订阅麦克风健康态(retain)，故障即现场红字+红闪
    client.publish(TOPIC_STATUS, "{\"status\":\"online\"}", true);   // retained：保证仪表盘任何时刻连上都判定在线
    LOGln("[MQTT] 已连接(retained状态)，已订阅 cmd + voice/cmd + voice/health，已上报 online");
  }
}

// ==================== 上报传感器 ====================
void mqtt_publish_sensors(float temp, float humid) {
  char json[256];
  sprintf(json, "{\"temp\":%.1f,\"humid\":%.1f}", temp, humid);
  // sprintf(json, "{\"battery\":%d,\"temp\":%.1f,\"humid\":%.1f}", battery, temp, humid);
  client.publish(TOPIC_SENSORS, json);
}

//==================== 上报GPS ====================
void mqtt_publish_gps(float lat, float lng){
  char json[256];
  sprintf(json, "{\"lat\":%.6f,\"lng\":%.6f}",lat, lng);
  client.publish(TOPIC_GPS, json);
}

// ==================== 初始化 ====================
void MQTT_set() {
  // 调试串口已在 setup() 中按 115200 初始化，这里不再重复 Serial.begin。

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);   // WiFi 掉线自动重连，避免设备失联
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // 带超时的 WiFi 连接，避免无热点时永久卡死（约 20 秒）
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000UL) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi 已连接，IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi 连接超时，将在 mqtt_loop 中继续重试");
  }

  // 增大 MQTT 缓冲，避免较长 screen_text 命令被截断
  client.setBufferSize(1024);
  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(callback);
  client.setKeepAlive(60);      // 60s 容忍：相机上传等短阻塞期间不被 broker 误判超时断开（减少误掉线）
  client.setSocketTimeout(2);   // 单次 connect/读 最多阻塞 2s，配合非阻塞重连避免冻结主循环
}

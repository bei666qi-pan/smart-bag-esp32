#ifndef SCREEN_H
#define SCREEN_H

#include <Arduino.h>

// [就近告警] 麦克风故障标志：屏幕已置红字，主循环据此驱动 LED 非阻塞红闪。
extern volatile bool g_micAlert;

//  控件名称对应（USART HMI / 串口屏，已与 chuankou Project.HMI 核对）：
//  - t_temperature  显示温度        - t_humidity   显示湿度
//  - t_light        灯光状态         - t_gps        GPS数据
//  - t_wifi         联网状态         - t5           用户上传文字
//  - t6             AI回复/思考       - class1..class4 / time1..time4  今日课表
//  按钮触摸回传（实测）：开灯=帧 65 00 05 01..  关灯=单字节 0x03  拍照=单字节 0x01

// ====================== 串口屏 UART 配置 ======================
// 重要：屏幕使用 ESP32-S3 的 UART1，GPS（NEO_M9N）使用 UART2，
// 二者不再共用 Serial2，彻底避免硬件串口冲突。
#define SCREEN_UART_NUM 1
#define SCREEN_TX   2    // GPIO2 → 屏幕 RX（原 GPIO42 是 JTAG 脚且该线已断，改接干净脚 GPIO2）
#define SCREEN_RX   48   // GPIO48 ← 屏幕 TX
#define SCREEN_BAUD 115200

void Screen_setup();        // 屏幕初始化
void handleScreenEvents();  // 处理屏幕按钮事件（须在 loop 中反复调用）
void Screen_readSensorData(double temp, double humid);

void updateTemperature(float temp);
void updateHumidity(float humi);
void updateLightStatus(bool isOn);
void updateGps(float lat, float lng);
void updateWifiStatus(bool connected);   // 联网状态 → t_wifi
void feedWaveform(float temp);           // 大蓝格波形：实时温度曲线
void updateWritingText(String text);
void updateAIResponse(String reply);
void updateTimetable(String class1, String time1, String class2, String time2,
                     String class3, String time3, String class4, String time4);
// [课表下行] 网页/MQTT 动态下发课表：value 形如 "<gbk名hex>,<HH:MM>;..."（最多 4 段，段内逗号分隔）。
// 课程名在网页侧已转成 GBK 字节再 hex，本机解码后直接写入 class1..4（GBK 字库控件）。
void setTimetableFromPayload(String value);
// [就近告警] 麦克风故障现场提示：on=屏幕红字「麦克风坏了」+ LED 红闪（由主循环驱动）；off=恢复待命态。
void screen_set_mic_alert(bool on);
// [语音上屏] 小乐语音状态上屏（与指示灯同步）：listening/thinking/idle → t6（AI/思考区）。
void screen_set_voice_state(String state);
void sendToScreen(String controlName, String value);
void sendEndMarker();
void takePhoto();
void screen_probe();   // 逆向探针：给 t0.txt..t29.txt 写 T0..T29，真机拍照定位控件号
void screen_raw(String cmd);   // 经 MQTT 向串口屏发任意 TJC 指令（dim/page/控件名探测等），便于免烧录调试

#endif

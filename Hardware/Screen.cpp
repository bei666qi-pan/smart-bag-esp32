#include "Hardware/Screen.h"
#include "Hardware/LED.h"
#include "Hardware/camera.h"
#include "Hardware/AHT10.h"
#include "Hardware/RemoteDebug.h"   // LOGf：把屏幕原始字节镜像到 telnet 远程日志，便于真机排查
#include <HardwareSerial.h>
#include <WiFi.h>                  // updateWifiStatus 读取 WiFi.status()
#include <time.h>                  // getLocalTime：NTP 同步后的实时时钟
#include "driver/gpio.h"           // gpio_reset_pin：释放 GPIO42(JTAG/MTMS) 复用，使其可作 UART TX

//  ==================== 屏幕专用串口（UART1） ====================
//  与 GPS（UART2）分离，避免硬件串口冲突。
HardwareSerial ScreenSerial(SCREEN_UART_NUM);

//  ==================== 全局状态 ====================
float current_temp = 25.5;         // 最近一次温度值
float current_humi = 60.0;         // 最近一次湿度值
bool  isLightOn    = false;        // 灯光状态
volatile bool g_micAlert = false;  // [就近告警] 麦克风故障：true→主循环驱动 LED 红闪

//  ==================== 科技蓝主题配色（白底高对比，RGB565）====================
//  注意：屏幕背景为白色，故全部采用深色系，确保在白底上清晰可读。
static const uint16_t CLR_NAVY   = 0x0010;  // 深藏蓝——课程名/主文字
static const uint16_t CLR_BLUE   = 0x001F;  // 蓝——湿度/温度常态
static const uint16_t CLR_TEAL   = 0x0410;  // 墨绿青——次要数值/GPS
static const uint16_t CLR_GREEN  = 0x0400;  // 深绿——灯开/在线
static const uint16_t CLR_ORANGE = 0xFC00;  // 橙——上课时间/偏热
static const uint16_t CLR_RED    = 0xF800;  // 红——高温/离线
static const uint16_t CLR_GRAY   = 0x8410;  // 灰——关闭/次要/定位中

//  发原始 TJC 指令（无引号，用于 .pco/.bco/ref 等属性设置）
void sendScreenCmd(String cmd) { ScreenSerial.print(cmd); sendEndMarker(); }
//  设置控件字体颜色（.pco），随后再写 .txt 即以新颜色重绘
void setScreenColor(String ctrl, uint16_t color) { sendScreenCmd(ctrl + ".pco=" + String(color)); }

//  NTP 同步后的本地时间字符串（未同步返回空）。例："06-15 周一 18:30"
String getClockStr() {
  struct tm t;
  if (!getLocalTime(&t, 30)) return "";
  static const char* wd[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
  char buf[48];
  sprintf(buf, "%02d-%02d %s %02d:%02d", t.tm_mon + 1, t.tm_mday, wd[t.tm_wday], t.tm_hour, t.tm_min);
  return String(buf);
}

//  ==================== 初始化 ====================
void Screen_setup() {
  // 先复位 TX 引脚、解除任何外设复用，再交给 UART1 当 TX（屏幕 RX 现接 GPIO2）。
  gpio_reset_pin((gpio_num_t)SCREEN_TX);
  pinMode(SCREEN_TX, OUTPUT);
  digitalWrite(SCREEN_TX, HIGH);   // UART 空闲态为高
  // UART1 用于串口屏；注意参数顺序为 (baud, config, RX, TX)
  ScreenSerial.begin(SCREEN_BAUD, SERIAL_8N1, SCREEN_RX, SCREEN_TX);

  Serial.println("屏幕初始化完成（UART1）");

  // 发送初始数据到屏幕
  updateTemperature(current_temp);
  updateHumidity(current_humi);
  updateLightStatus(isLightOn);
  updateWritingText("");
  updateAIResponse("");

  // 填充今日课表。class1..class4 控件用的是 GBK 字库（与温度等 UTF-8 控件不同字库），
  // 故课程名必须发 GBK 字节，否则乱码：语文=d3efcec4 数学=cafdd1a7 英语=d3a2d3ef 科学=bfc6d1a7
  updateTimetable("\xd3\xef\xce\xc4", "08:00", "\xca\xfd\xd1\xa7", "09:40",
                  "\xd3\xa2\xd3\xef", "10:30", "\xbf\xc6\xd1\xa7", "14:00");
  updateWifiStatus(WiFi.status() == WL_CONNECTED);   // 启用 t_wifi：联网状态

  Serial.println("屏幕已启动，等待屏幕按钮事件...");
}

//  ==================== 屏幕事件处理 ====================
//  须在主 loop() 中反复调用，否则按钮事件无法响应。
// 执行一个已识别的屏幕按钮（b0=开灯 b1=关灯 b2=拍照）；字符串/二进制两种来源共用
static void dispatchScreenButton(const String& btn) {
  if (btn == "b0") {
    Serial.println("开灯按钮被按下"); LOGln("[SCREEN] b0 开灯");
    isLightOn = true; updateLightStatus(isLightOn); led_on(0, 255, 0);
  } else if (btn == "b1") {
    Serial.println("关灯按钮被按下"); LOGln("[SCREEN] b1 关灯");
    isLightOn = false; updateLightStatus(isLightOn); led_all_off();
  } else if (btn == "b2") {
    Serial.println("拍照按钮被按下"); LOGln("[SCREEN] b2 拍照");
    takePhoto();
  }
}

void handleScreenEvents() {
  if (!ScreenSerial.available()) return;
  // 收一帧：直到出现 ~40ms 空隙或缓冲满（一次按下的字节会成簇到达）
  uint8_t buf[64]; int n = 0; uint32_t t = millis();
  while (millis() - t < 40 && n < (int)sizeof(buf)) {
    while (ScreenSerial.available() && n < (int)sizeof(buf)) { buf[n++] = (uint8_t)ScreenSerial.read(); t = millis(); }
  }
  if (n == 0) return;

  // 屏幕实测按钮协议（2026-06-15 用时间戳受控标定，基线静默、按时才回传）：
  //   开灯 → 触摸帧 65 00 05 01 FF FF FF（comp=5；原以为是周期噪声，实为开灯键）
  //   关灯 → 单字节 0x03
  //   拍照 → 单字节 0x01
  const char* btn = nullptr;
  if (n >= 4 && buf[0] == 0x65 && buf[2] == 0x05) btn = "b0";          // 开灯
  else if (n == 1 && buf[0] == 0x03)              btn = "b1";          // 关灯
  else if (n == 1 && buf[0] == 0x01)              btn = "b2";          // 拍照
  if (!btn) return;                                                     // 其余静默忽略

  // 去抖：同一键 600ms 内只触发一次（一次按下可能成多帧；尤其防止拍照被重复触发）
  static uint32_t lastBtnMs = 0;
  static String lastBtn;
  uint32_t now = millis();
  if (lastBtn == btn && now - lastBtnMs < 600) return;
  lastBtnMs = now; lastBtn = btn;

  dispatchScreenButton(String(btn));
}

// ==================== 屏幕数据发送函数 ====================

//  更新温度显示  控件: t_temperature
//  异常可见性：读数为哨兵值(9999)或越界，屏上显示"-- 传感器故障"(红)，不再把 9999.0 当真实温度误导用户。
void updateTemperature(float temp) {
  if (temp >= 9000.0f || temp < -40.0f || temp > 85.0f) {
    setScreenColor("t_temperature", CLR_RED);
    sendToScreen("t_temperature.txt", "温度: -- 传感器故障");
    return;
  }
  // 颜色编码：<18 偏冷=蓝，18~28 常态=墨绿青，>28 偏热=红
  uint16_t c = (temp < 18.0f) ? CLR_BLUE : (temp > 28.0f ? CLR_RED : CLR_TEAL);
  setScreenColor("t_temperature", c);
  String text = "温度: " + String(temp, 1) + "°C ";
  sendToScreen("t_temperature.txt", text);
}

//  更新湿度显示  控件: t_humidity
//  异常可见性：同温度，哨兵/越界显示"-- 传感器故障"(红)。
void updateHumidity(float humi) {
  if (humi >= 9000.0f || humi < 0.0f || humi > 100.0f) {
    setScreenColor("t_humidity", CLR_RED);
    sendToScreen("t_humidity.txt", "湿度: -- 传感器故障");
    return;
  }
  setScreenColor("t_humidity", CLR_BLUE);
  const char* tag = (humi < 30.0f) ? " 干燥" : (humi > 70.0f ? " 潮湿" : " 适中");
  String text = "湿度: " + String(humi, 1) + "%" + tag;
  sendToScreen("t_humidity.txt", text);
}

//  更新灯光状态  控件: t_light
void updateLightStatus(bool isOn) {
  setScreenColor("t_light", isOn ? CLR_GREEN : CLR_GRAY);
  String status = isOn ? "灯光: 开" : "灯光: 关";
  sendToScreen("t_light.txt", status);
  Serial.print("灯光状态更新: ");
  Serial.println(status);
}

//  更新 GPS 显示  控件: t_gps
void updateGps(float lat, float lng) {
  if (lat == 0.0 && lng == 0.0) {
    // 室内无定位时，把这个原本"定位中..."的框用来显示 NTP 实时时钟，不浪费
    String clk = getClockStr();
    if (clk.length()) {
      setScreenColor("t_gps", CLR_TEAL);
      sendToScreen("t_gps.txt", "时间 " + clk);
    } else {
      setScreenColor("t_gps", CLR_GRAY);
      sendToScreen("t_gps.txt", "GPS: 定位中...");
    }
  } else {
    setScreenColor("t_gps", CLR_TEAL);
    String text = "GPS: " + String(lat, 6) + "," + String(lng, 6);
    sendToScreen("t_gps.txt", text);
  }
}

//  更新联网状态  控件: t_wifi（此前固件未使用，本次启用）
void updateWifiStatus(bool connected) {
  setScreenColor("t_wifi", connected ? CLR_GREEN : CLR_RED);
  sendToScreen("t_wifi.txt", connected ? "WiFi: 已连接" : "WiFi: 断开");
}

//  更新用户上传文字  控件: t5（真机探针确认：屏幕工程里写字框是 t5，非 t_writing）
void updateWritingText(String text) {
  sendToScreen("t5.txt", text);
  Serial.print("更新用户文字: ");
  Serial.println(text);
}

//  更新 AI 回复内容  控件: t6（真机探针确认：AI回复框是 t6，非 t_replay）
void updateAIResponse(String reply) {
  sendToScreen("t6.txt", reply);
  Serial.print("AI回复已发送: ");
  Serial.println(reply);
}

//  更新今日课表（控件名需与屏幕工程一致）
void updateTimetable(String class1, String time1, String class2, String time2,
                     String class3, String time3, String class4, String time4) {
  // 真实控件名为 class1..class4 / time1..time4（此前误用 class1_text/time1_text → 课表一直空白）。
  // 课程名白色、上课时间黄色，科技蓝深色主题下层次分明。
  const char* cls[4] = {"class1", "class2", "class3", "class4"};
  const char* tim[4] = {"time1",  "time2",  "time3",  "time4"};
  String cv[4] = {class1, class2, class3, class4};
  String tv[4] = {time1,  time2,  time3,  time4};
  for (int i = 0; i < 4; i++) {
    setScreenColor(cls[i], CLR_NAVY);   sendToScreen(String(cls[i]) + ".txt", cv[i]);
    setScreenColor(tim[i], CLR_ORANGE); sendToScreen(String(tim[i]) + ".txt", tv[i]);
  }
  Serial.println("课表已更新");
}

//  [课表下行] 把一段十六进制字符串还原成原始字节串（GBK 字节）。
//  例 "d3efcec4" → 0xD3 0xEF 0xCE 0xC4（"语文"的 GBK）。非法/奇数长度按位丢弃，尽量不崩。
static String hexToBytes(const String& hex) {
  String out;
  int n = hex.length() & ~1;   // 取偶数长度
  for (int i = 0; i < n; i += 2) {
    auto nib = [](char ch) -> int {
      if (ch >= '0' && ch <= '9') return ch - '0';
      if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
      if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
      return -1;
    };
    int hi = nib(hex[i]), lo = nib(hex[i + 1]);
    if (hi < 0 || lo < 0) continue;
    out += (char)((hi << 4) | lo);
  }
  return out;
}

//  [课表下行] 解析网页/MQTT 下发的课表 payload，动态写入 class1..4 / time1..4。
//  value 形如 "d3efcec4,08:00;cafdd1a7,09:40;..."（分号分段，最多 4 段；段内逗号分隔 课程名hex,时间）。
//  课程名已在网页侧转成 GBK 再 hex（class 控件是 GBK 字库），此处解码后原样写入，避免 ESP32 端做 UTF-8→GBK。
void setTimetableFromPayload(String value) {
  const char* cls[4] = {"class1", "class2", "class3", "class4"};
  const char* tim[4] = {"time1",  "time2",  "time3",  "time4"};
  int slot = 0;
  int from = 0;
  while (slot < 4 && from <= value.length()) {
    int semi = value.indexOf(';', from);
    String seg = (semi < 0) ? value.substring(from) : value.substring(from, semi);
    seg.trim();
    if (seg.length() > 0) {
      int comma = seg.indexOf(',');
      String nameHex = (comma < 0) ? seg : seg.substring(0, comma);
      String tm      = (comma < 0) ? ""  : seg.substring(comma + 1);
      nameHex.trim(); tm.trim();
      String nameGbk = hexToBytes(nameHex);
      setScreenColor(cls[slot], CLR_NAVY);   sendToScreen(String(cls[slot]) + ".txt", nameGbk);
      setScreenColor(tim[slot], CLR_ORANGE); sendToScreen(String(tim[slot]) + ".txt", tm);
      LOGf("[TIMETABLE] slot%d name(%d字节) time=%s\n", slot + 1, nameGbk.length(), tm.c_str());
    }
    slot++;
    if (semi < 0) break;
    from = semi + 1;
  }
  Serial.println("课表已按下发内容更新");
}

//  [就近告警] 麦克风故障现场提示：on→屏幕 t5 红字「麦克风坏了」(GBK) + 置位 g_micAlert(主循环红闪 LED)；
//  off→恢复 t5 待机字「智能书包」并清标志。书包旁的人无需登录网页即可知道"该去拔插 USB 麦克风"。
void screen_set_mic_alert(bool on) {
  if (on) {
    g_micAlert = true;
    setScreenColor("t5", CLR_RED);
    sendToScreen("t5.txt", "\xc2\xf3\xbf\xcb\xb7\xe7\xbb\xb5\xc1\xcb");   // GBK「麦克风坏了」
    LOGln("[MIC] 故障告警：屏幕红字 + LED 红闪");
  } else {
    g_micAlert = false;
    setScreenColor("t5", CLR_NAVY);
    sendToScreen("t5.txt", "\xd6\xc7\xc4\xdc\xca\xe9\xb0\xfc");           // GBK「智能书包」
    LOGln("[MIC] 故障解除：恢复待机显示");
  }
}

//  [语音上屏] 小乐语音状态与指示灯同步上屏到 t6（AI/思考区，GBK 字库）：
//  listening→「在听中」 thinking→「思考中」 idle→「小乐待命中」。日光下看不清 LED 时，屏幕也能看出在听/在想。
void screen_set_voice_state(String state) {
  setScreenColor("t6", CLR_NAVY);
  if (state == "listening")     sendToScreen("t6.txt", "\xd4\xda\xcc\xfd\xd6\xd0");           // 在听中
  else if (state == "thinking") sendToScreen("t6.txt", "\xcb\xbc\xbf\xbc\xd6\xd0");           // 思考中
  else                          sendToScreen("t6.txt", "\xd0\xa1\xc0\xd6\xb4\xfd\xc3\xfc\xd6\xd0"); // 小乐待命中
}

//  通用屏幕发送函数
//  @param controlName 控件名称（如 "t_temperature.txt"）
//  @param value       要设置的值
void sendToScreen(String controlName, String value) {
  ScreenSerial.print(controlName);
  ScreenSerial.print("=\"");
  ScreenSerial.print(value);
  ScreenSerial.print("\"");
  sendEndMarker();
}

//  发送 USART HMI 指令结束标志（三个 0xFF）
void sendEndMarker() {
  ScreenSerial.write(0xFF);
  ScreenSerial.write(0xFF);
  ScreenSerial.write(0xFF);
}

//  ==================== 波形曲线（书包状态大蓝格）====================
//  大蓝格是波形控件(s)，但喂数据要用数字 id 而非名字。探测确认它会响应 add，
//  故直接把温度喂给 id 0..31 全部：add 对非波形控件无效会被忽略，只有那个格子真正绘制。
//  通道0=温度(10~42℃→0~255)。首次铺 40 点填满，之后每周期加 1 点形成滚动趋势。
void feedWaveform(float temp) {
  // 波形方案已放弃：大蓝格是波形控件，但其通道色(pco0)不支持运行时修改→只能停在设计态红色、效果差。
  // 本函数仅保留"一次性待机文字"初始化（开机发会丢，故在首个上报周期发一次）。
  (void)temp;
  static bool inited = false;
  if (inited) return;
  inited = true;
  // t5/t6 是 GBK 字库 + txt_maxl 仅~5汉字（设计态固定），故用短词 GBK：t5="智能书包" t6="小乐待命中"
  setScreenColor("t5", CLR_NAVY); sendToScreen("t5.txt", "\xd6\xc7\xc4\xdc\xca\xe9\xb0\xfc");
  setScreenColor("t6", CLR_NAVY); sendToScreen("t6.txt", "\xd0\xa1\xc0\xd6\xb4\xfd\xc3\xfc\xd6\xd0");
}

//  ==================== 拍照功能 ====================
void takePhoto() {
  Serial.println("拍照功能被调用");
  updateWritingText("正在拍照...");
  camera_system_capture_now();   // 立即抓拍并上传一次
  updateWritingText("拍照完成");
}

//  ==================== 逆向探针 ====================
//  给 t0.txt..t29.txt 依次写 "T0".."T29"。真机拍照后即可看出哪个框对应哪个控件号，
//  从而把本文件里的控件名改成与 TJC 屏幕工程一致。由 MQTT 命令 action=screen_probe 触发。
void screen_probe() {
  for (int i = 0; i < 61; i++) {
    sendToScreen("t" + String(i) + ".txt", "T" + String(i));
    delay(20);
  }
  LOGln("[SCREEN] 探针已发送 t0.txt..t60.txt = T0..T60（请拍屏幕照片）");
}

//  经 MQTT 透传任意 TJC 指令到串口屏（自动补 0xFFx3 结束符）。
//  例：screen_raw("dim=20") 调背光（与控件名无关，可判 TX 是否真的送达屏幕）。
void screen_raw(String cmd) {
  // 特殊：TX 引脚扫描——依次把屏幕 TX 切到候选引脚，各发 dim=10 保持 4s，定位真实屏幕RX脚
  if (cmd == "sweeptx") {
    const int pins[] = {42, 41, 40, 2, 1, 8, 9, 46};
    const int N = sizeof(pins) / sizeof(pins[0]);
    LOGln("[SWEEP] 开始 TX 引脚扫描，请盯着屏幕看哪一下变暗");
    for (int i = 0; i < N; i++) {
      int p = pins[i];
      ScreenSerial.begin(SCREEN_BAUD, SERIAL_8N1, SCREEN_RX, p);   // 只改 TX，RX 仍 48
      delay(120);
      LOGf("[SWEEP] (%d/%d) TX=GPIO%d → dim=10 保持4s\n", i + 1, N, p);
      ScreenSerial.print("dim=10"); sendEndMarker();
      delay(4000);
      ScreenSerial.print("dim=100"); sendEndMarker();   // 恢复（若此脚正确，屏幕会亮回）
      delay(900);
    }
    ScreenSerial.begin(SCREEN_BAUD, SERIAL_8N1, SCREEN_RX, SCREEN_TX);  // 恢复默认 TX=42
    ScreenSerial.print("dim=100"); sendEndMarker();
    LOGln("[SWEEP] 扫描结束，已恢复 TX=GPIO42");
    return;
  }
  // 特殊：波形控件 id 探测——循环给 id 0..31 画满幅斜坡，同时在 t5 显示当前 id。
  // 盯着大蓝格，出现"斜坡上升线"时记下文字框里的 id，即波形控件的数字 id。
  if (cmd == "waveprobe") {
    LOGln("[WAVE] 循环探测波形 id 0..31，盯大蓝格出现斜坡时记下文字框的 id");
    for (int id = 0; id <= 31; id++) {
      sendToScreen("t5.txt", "wave id=" + String(id));
      sendScreenCmd("cle " + String(id) + ",0");
      for (int p = 0; p <= 100; p++) sendScreenCmd("add " + String(id) + ",0," + String((p * 250) / 100));
      delay(1500);
    }
    sendToScreen("t5.txt", "wave done");
    LOGln("[WAVE] 探测结束");
    return;
  }
  ScreenSerial.print(cmd);
  sendEndMarker();
  LOGf("[SCREEN] raw已发送: %s\n", cmd.c_str());
}

//  ==================== 传感器读取（由主循环传入实时值） ====================
void Screen_readSensorData(double temp, double humid) {
  current_temp = (float)temp;
  current_humi = (float)humid;
  updateTemperature(current_temp);
  updateHumidity(current_humi);
}

#include "Hardware/RemoteDebug.h"
#include <WiFi.h>
#include <cstdarg>

// telnet 服务端 + 单个客户端（联调一般一个观察端足够）
static WiFiServer telnetServer(TELNET_PORT);
static WiFiClient telnetClient;
static bool started = false;

void rdbg_setup() {
  telnetServer.begin();
  telnetServer.setNoDelay(true);
  started = true;
  Serial.printf("[telnet] 远程日志已启动: telnet %s %d\n",
                WiFi.localIP().toString().c_str(), TELNET_PORT);
}

void rdbg_handle() {
  if (!started) return;

  // 有新连接：替换为最新的观察端
  if (telnetServer.hasClient()) {
    WiFiClient incoming = telnetServer.available();
    if (telnetClient && telnetClient.connected()) {
      telnetClient.stop();
    }
    telnetClient = incoming;
    telnetClient.println("=== 智能书包 远程日志已连接 [OTA-v29 mqttauth] ===");
  }

  // 丢弃远程输入，避免缓冲堆积
  if (telnetClient && telnetClient.connected()) {
    while (telnetClient.available()) telnetClient.read();
  }
}

bool rdbg_clientConnected() {
  return telnetClient && telnetClient.connected();
}

void rdbg_print(const String& s) {
  if (rdbg_clientConnected()) telnetClient.print(s);
}

void rdbg_println(const String& s) {
  if (rdbg_clientConnected()) telnetClient.println(s);
}

void LOGln(const String& s) {
  Serial.println(s);
  rdbg_println(s);
}

void LOGf(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
  rdbg_print(String(buf));
}

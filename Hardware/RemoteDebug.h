#ifndef REMOTE_DEBUG_H
#define REMOTE_DEBUG_H

#include <Arduino.h>

// ====================== telnet 远程日志 ======================
// 作用：WiFi 连上后开一个 telnet 服务（端口 23），把关键联调日志推到远程。
// 这样一次 USB 烧录后，就能用 `telnet <esp32-ip> 23` 或 `nc <esp32-ip> 23`
// 远程实时看日志，不用再插数据线看 USB 串口。
#define TELNET_PORT 23

void rdbg_setup();              // 启动 telnet 日志服务（须在 WiFi 连上后调用）
void rdbg_handle();             // 维护 telnet 客户端（须在 loop 中反复调用）
bool rdbg_clientConnected();    // 是否有远程客户端在看

void rdbg_print(const String& s);
void rdbg_println(const String& s);

// 同时输出到 USB 串口 + telnet 的日志助手（联调关键点用它）
void LOGln(const String& s);
void LOGf(const char* fmt, ...);

#endif

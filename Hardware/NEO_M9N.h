#ifndef NEO_M9N_H
#define NEO_M9N_H


// 引脚定义
#define RXPin 16
#define TXPin 17

// GPS 串口配置（使用 ESP32 硬件串口2）
#define GPS_UART_NUM 2
#define GPS_BAUDRATE 38400

// 超时时间
#define TIMEOUT_MS 10000

// 外部全局对象声明
extern TinyGPSPlus gps;
extern HardwareSerial MyGPSserial;
extern unsigned long lastValidLocationTime;

// 函数声明
void NEO_M9N_Init();            // GPS 初始化函数
void NEO_M9N_Update();          // GPS数据更新（读取串口 + 编码）
void NEO_M9N_ReadLocation(float &lat, float &lng);   // 检查定位状态 & 传出数据

#endif

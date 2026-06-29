#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include "Hardware/NEO_M9N.h"

// 定义全局对象
TinyGPSPlus gps;
HardwareSerial MyGPSserial(GPS_UART_NUM);
unsigned long lastValidLocationTime = 0;

// GPS 初始化函数
void NEO_M9N_Init() {
  // 注意：不要在这里 Serial.begin(38400)，否则会把调试串口波特率从 115200
  // 改成 38400，导致主串口日志乱码。调试串口在 setup() 中已按 115200 初始化。
  // GPS 硬件串口（UART2）初始化
  MyGPSserial.begin(GPS_BAUDRATE, SERIAL_8N1, RXPin, TXPin);
}

// GPS数据更新（读取串口 + 编码）
void NEO_M9N_Update() {
  while (MyGPSserial.available()) {
    char c = MyGPSserial.read();
    // Serial.write(c);  // 转发原始GPS数据
    gps.encode(c);    // 解析数据
  }
}

// 检查定位状态 & 传出数据
void NEO_M9N_ReadLocation(float &lat, float &lng) {
  if (gps.location.isValid()) {
    lat = gps.location.lat();
    lng = gps.location.lng();

    // Serial.println("=== 定位成功 ===");
    // Serial.printf("纬度: %.6f, 经度: %.6f\n", gps.location.lat(), gps.location.lng());
    // Serial.printf("卫星数: %d\n", gps.satellites.value());

    lastValidLocationTime = millis();
  } else {
    if (millis() - lastValidLocationTime > TIMEOUT_MS) {
      lat = 0.0;
      lng = 0.0;// 数据异常经纬度输出0.0
      Serial.println("未获得有效定位，请移至室外空旷处！");
      lastValidLocationTime = millis();
    }
  }
}
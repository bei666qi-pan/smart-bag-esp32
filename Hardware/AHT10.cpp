#include <Wire.h>
#include "Hardware/AHT10.h"

// 手动初始化AHT10
bool aht10_init() {
  Wire.beginTransmission(AHT10_ADDR);
  Wire.write(AHT10_CMD_CALIBRATE); // 发送校准指令
  Wire.write(0x08);                // 校准参数1
  Wire.write(0x00);                // 校准参数2
  if (Wire.endTransmission() != 0) {
    return false; // 校准失败
  }
  delay(100);     // 校准等待
  return true;
}

// 手动读取温湿度数据
bool aht10_read(float &temp, float &hum) {
  // 1. 发送触发测量指令
  Wire.beginTransmission(AHT10_ADDR);
  Wire.write(AHT10_CMD_TRIGGER);
  Wire.write(0x33);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  delay(80); // 等待测量完成（AHT10测量需≥75ms）

  // 2. 读取6字节数据
  Wire.requestFrom(AHT10_ADDR, 6);
  if (Wire.available() != 6) {
    return false;
  }

  // 3. 解析数据（按原厂手册公式）
  uint8_t data[6];
  for (int i = 0; i < 6; i++) {
    data[i] = Wire.read();
  }

  // 检查状态位（跳过忙状态）
  if (data[0] & 0x80) {
    return false;
  }

  // 计算湿度（0~100%）
  uint32_t hum_raw = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | (data[3] >> 4);
  hum = (float)hum_raw / 0x100000 * 100.0;

  // 计算温度（-40~85℃）
  uint32_t temp_raw = ((uint32_t)(data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | data[5];
  temp = (float)temp_raw / 0x100000 * 200.0 - 50.0;

  return true;
}

// I2C 总线恢复：从机若把 SDA 卡在低电平（被异常时序/掉电中断打断），
// 主机手动脉冲 SCL 最多 9 次让从机把当前字节发完并释放 SDA，再补一个 STOP。
static void i2c_bus_recover() {
  pinMode(AHT10_SDA, INPUT_PULLUP);
  pinMode(AHT10_SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(AHT10_SCL, HIGH);
  delayMicroseconds(10);
  for (int i = 0; i < 9 && digitalRead(AHT10_SDA) == LOW; i++) {
    digitalWrite(AHT10_SCL, LOW);  delayMicroseconds(10);
    digitalWrite(AHT10_SCL, HIGH); delayMicroseconds(10);
  }
  // 生成 STOP 条件：SCL 高时 SDA 由低变高
  pinMode(AHT10_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(AHT10_SDA, LOW);  delayMicroseconds(10);
  digitalWrite(AHT10_SCL, HIGH); delayMicroseconds(10);
  digitalWrite(AHT10_SDA, HIGH); delayMicroseconds(10);
}

void aht10_setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  i2c_bus_recover();     // 先释放可能被卡死的 I2C 总线（应对偶发挂死）

  // 初始化I2C（强制低速+延时）
  Wire.begin(AHT10_SDA, AHT10_SCL);
  Wire.setClock(100000); // 强制100kHz
  delay(300);            // 给AHT10充足上电时间

  // 软复位AHT10
  Wire.beginTransmission(AHT10_ADDR);
  Wire.write(AHT10_CMD_RESET);
  Wire.endTransmission();
  delay(50);

  // 初始化AHT10：以"能否真正读到数据"为成功标准（calibrate 偶发 NACK 但实际可读）；
  // 失败则总线恢复后重试，最多 3 次
  bool ok = false;
  for (int i = 0; i < 3 && !ok; i++) {
    aht10_init();
    float t, h;
    ok = aht10_read(t, h);
    if (!ok) { i2c_bus_recover(); Wire.begin(AHT10_SDA, AHT10_SCL); Wire.setClock(100000); delay(100); }
  }
  Serial.println(ok ? "AHT10 初始化成功！" : "AHT10 初始化失败（已尝试总线恢复）");
}

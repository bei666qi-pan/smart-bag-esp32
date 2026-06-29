#ifndef AHT10_H
#define AHT10_H

#define AHT10_SDA 8
#define AHT10_SCL 9

#define AHT10_ADDR 0x38 // AHT10固定I2C地址

// AHT10指令定义（原厂手册）
#define AHT10_CMD_CALIBRATE 0xE1
#define AHT10_CMD_TRIGGER   0xAC
#define AHT10_CMD_RESET     0xBA

bool aht10_init(); // 手动初始化AHT10
bool aht10_read(float &temp, float &hum); // 手动读取温湿度数据
void aht10_setup();

#endif

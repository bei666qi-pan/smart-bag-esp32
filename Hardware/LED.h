#ifndef LED_H
#define LED_H

// ====================== LED硬件配置 ======================
#define LED_PIN        18       // 灯带数据引脚
#define NUM_LEDS       120      // 灯珠总数
#define CHIPSET        WS2812B  // 灯带芯片型号
#define COLOR_ORDER    GRB      // WS2812B默认颜色顺序：GRB

// ====================== 函数声明 ======================
// LED初始化函数
void led_init(uint32_t light);
// 常亮
void led_on(uint32_t rVal, uint32_t gVal, uint32_t bVal);
// 流苏追逐（带余晖拖尾）
void led_chase(uint32_t rVal, uint32_t gVal, uint32_t bVal);
// 呼吸灯效果
void led_breathing(uint32_t rVal, uint32_t gVal, uint32_t bVal);
// 全部关灯
void led_all_off();

#endif

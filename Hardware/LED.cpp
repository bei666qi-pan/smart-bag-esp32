#include <FastLED.h>
#include "Hardware/LED.h"

// 定义LED灯珠数组
CRGB leds[NUM_LEDS];
// 虚拟亮度数组：用于实现流苏余晖渐变效果
float virtual_brightness[NUM_LEDS] = {0};

// ====================== LED初始化 ======================
void led_init(uint32_t light) {
    delay(1000);  // 上电延时，稳定电源
    // 绑定灯带类型、引脚、颜色顺序
    FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setBrightness(light);  // 设置全局最大亮度(0-255)
}

// ====================== 常亮 ======================
void led_on(uint32_t rVal, uint32_t gVal, uint32_t bVal) {
    // 设置全部灯珠的颜色
    fill_solid(leds, NUM_LEDS, CRGB(rVal, gVal, bVal));
    FastLED.show();  // 刷新输出到灯带
}

// ====================== 流苏追逐（带余晖） ======================
void led_chase(uint32_t rVal, uint32_t gVal, uint32_t bVal) {
    // 上一次更新时间戳，用于控制帧率
    static uint32_t last_update = 0;
    const uint32_t frame_interval = 8;    // 每帧间隔(ms)，值越小越快
    const float chase_speed = 0.6f;       // 流苏移动速度

    // 控制刷新频率，防止跑太快
    if (millis() - last_update < frame_interval) return;
    last_update = millis();

    // 流苏当前位置（浮点型，保证平滑移动）
    static float pos = 0.0f;
    const int group_size = 40;     // 单条流苏长度
    const int fade_step = 10;      // 亮度衰减步长
    const int period = 40;         // 流苏之间的间隔
    const float decay = 0.85f;     // 余晖衰减系数

    // 1. 所有灯珠亮度衰减，实现拖尾余晖
    for (int i = 0; i < NUM_LEDS; i++) {
        virtual_brightness[i] *= decay;
        if (virtual_brightness[i] < 0.1f) virtual_brightness[i] = 0;
    }

    // 2. 计算并生成新的流苏光束
    int first_stream = (int)pos - (((int)pos / period) + 2) * period;
    for (int stream = first_stream; stream < NUM_LEDS; stream += period) {
        for (int i = 0; i < group_size; i++) {
            int idx = stream + i;
            // 越界判断，防止程序崩溃
            if (idx < 0 || idx >= NUM_LEDS) continue;
            
            // 计算当前灯珠目标亮度
            float new_brightness = 255.0f - i * fade_step;
            if (new_brightness < 0) new_brightness = 0;
            
            // 只保留更亮的值，实现叠加效果
            if (virtual_brightness[idx] < new_brightness) {
                virtual_brightness[idx] = new_brightness;
            }
        }
    }

    // 3. 将虚拟亮度映射为实际所需要的灯珠颜色
    for (int i = 0; i < NUM_LEDS; i++) {
        uint8_t r = (uint8_t)fminf(rVal * virtual_brightness[i], 255.0f);
        uint8_t g = (uint8_t)fminf(gVal * virtual_brightness[i], 255.0f);
        uint8_t b = (uint8_t)fminf(bVal *virtual_brightness[i], 255.0f);
        leds[i] = CRGB(r, g, b);  // 纯蓝色
    }

    FastLED.show();  // 输出显示
    pos += chase_speed;  // 移动流苏位置
}

// ====================== 呼吸灯 ======================
void led_breathing(uint32_t rVal, uint32_t gVal, uint32_t bVal) {
    static uint8_t step = 0;          // 呼吸渐变步数
    const uint8_t steps = 100;       // 总步数
    const uint32_t cycle_ms = 2000;  // 一个完整呼吸周期5秒

    // 正弦曲线计算亮度，实现平滑呼吸效果
    float ratio = (1.0 + sin((2 * PI * step) / steps - PI/2)) / 2.0;
    uint8_t brightness = (uint8_t)(255 * ratio);

    // 设置全部灯珠为所需要的颜色，亮度随正弦值变化
    uint8_t r = rVal * (brightness / 255.0f);
    uint8_t g = gVal * (brightness / 255.0f);
    uint8_t b = bVal * (brightness / 255.0f);

    fill_solid(leds, NUM_LEDS, CRGB(r,g, b));
    FastLED.show();

    step = (step + 1) % steps;  // 循环步数
    delay(cycle_ms / steps);    // 控制周期速度
}

// ====================== 全部关灯 ======================
void led_all_off() {
    fill_solid(leds, NUM_LEDS, CRGB::Black);  // 黑色=关灯
    FastLED.show();
}
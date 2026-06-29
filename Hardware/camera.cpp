#include "camera.h"
#include <esp_camera.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "../secrets.h"   // 设备令牌（已在 .gitignore，见 secrets.h.example）

// ==================== 云平台上传配置（按需修改） ====================
const char* serverUrl    = "https://bag.versecraft.cn/api/camera/latest";
const char* deviceToken  = SECRET_DEVICE_TOKEN;
const unsigned long uploadInterval = 30000;   // 上传间隔（毫秒）；30s（用户确认够用）：降低同步上传阻塞、避免饿死 OTA/MQTT/telnet

// WiFi 的 SSID/密码已在 MQTT 模块中配置，此处不再重复


// ==================== 内部实现 ====================
static bool camera_driver_init() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = CAM_PIN_D0;
    config.pin_d1 = CAM_PIN_D1;
    config.pin_d2 = CAM_PIN_D2;
    config.pin_d3 = CAM_PIN_D3;
    config.pin_d4 = CAM_PIN_D4;
    config.pin_d5 = CAM_PIN_D5;
    config.pin_d6 = CAM_PIN_D6;
    config.pin_d7 = CAM_PIN_D7;
    config.pin_xclk = CAM_PIN_XCLK;
    config.pin_pclk = CAM_PIN_PCLK;
    config.pin_vsync = CAM_PIN_VSYNC;
    config.pin_href = CAM_PIN_HREF;
    config.pin_sscb_sda = CAM_PIN_SIOD;
    config.pin_sscb_scl = CAM_PIN_SIOC;
    config.pin_pwdn = CAM_PIN_PWDN;
    config.pin_reset = CAM_PIN_RESET;
    config.xclk_freq_hz = 20000000;      // 标准 20MHz（16MHz 经测对竖线无改善，已还原）
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_VGA;   // QVGA(320x240)→VGA(640x480)：4倍像素，画面更清晰
    config.jpeg_quality = 12;
    config.fb_count = 2;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("摄像头初始化失败，错误码: 0x%x\n", err);
        return false;
    }
    Serial.println("摄像头初始化成功");
    // 彩条诊断已确认传感器+排线健康（彩条干净无竖线）；恢复正常成像。
    // 顺手优化画质：set_quality 更高(数值更小)以减轻锐利边缘的 JPEG 彩色镶边；自动白平衡/增益用默认。
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_colorbar(s, 0);          // 关闭彩条，恢复真实成像
        s->set_quality(s, 8);           // JPEG 质量 12→8（更高），减轻边缘彩色镶边
    }
    return true;
}

static camera_fb_t* camera_capture() {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("拍照失败");
        return nullptr;
    }
    Serial.printf("拍照成功，大小: %zu 字节\n", fb->len);
    return fb;
}

static bool camera_upload_photo(camera_fb_t* fb) {
    if (!fb || fb->len == 0) {
        Serial.println("空帧，跳过上传");
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    if (!http.begin(client, serverUrl)) {
        Serial.println("HTTP begin 失败");
        return false;
    }

    http.addHeader("x-device-token", deviceToken);

    String boundary = "ESP32BagCamBoundary";
    String bodyStart = "--" + boundary + "\r\n";
    bodyStart += "Content-Disposition: form-data; name=\"image\"; filename=\"photo.jpg\"\r\n";
    bodyStart += "Content-Type: image/jpeg\r\n\r\n";
    String bodyEnd = "\r\n--" + boundary + "--\r\n";

    size_t totalLen = bodyStart.length() + fb->len + bodyEnd.length();
    uint8_t* body = (uint8_t*)malloc(totalLen);
    if (!body) {
        Serial.println("内存分配失败");
        http.end();
        return false;
    }

    size_t pos = 0;
    memcpy(body + pos, bodyStart.c_str(), bodyStart.length());
    pos += bodyStart.length();
    memcpy(body + pos, fb->buf, fb->len);
    pos += fb->len;
    memcpy(body + pos, bodyEnd.c_str(), bodyEnd.length());

    String contentType = "multipart/form-data; boundary=" + boundary;
    http.addHeader("Content-Type", contentType);

    Serial.printf("正在上传 %zu 字节...\n", totalLen);
    int httpCode = http.POST(body, totalLen);
    free(body);

    bool success = false;
    if (httpCode > 0) {
        String resp = http.getString();
        Serial.printf("上传完成，HTTP %d，响应: %s\n", httpCode, resp.c_str());
        if (httpCode == 200) success = true;
    } else {
        Serial.printf("上传失败: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
    return success;
}

// ==================== 对外接口 ====================
void camera_system_init() {
    // 依赖 MQTT 模块已经完成 WiFi 连接
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("错误：WiFi 未连接，摄像头无法初始化");
        while (1) delay(1000);
    }
    if (!camera_driver_init()) {
        while (1) delay(1000);
    }
}

void camera_system_loop() {
    static unsigned long lastUpload = 0;
    static int failCount = 0;
    unsigned long now = millis();

    if (now - lastUpload >= uploadInterval) {
        lastUpload = now;

        camera_fb_t* fb = camera_capture();
        if (fb) {
            failCount = 0;
            camera_upload_photo(fb);
            esp_camera_fb_return(fb);
        } else {
            failCount++;
            Serial.printf("拍照失败 (%d/3)\n", failCount);
            if (failCount >= 3) {
                Serial.println("摄像头连续失败3次，开始深度复位...");
                // 1. 先彻底反初始化
                esp_camera_deinit();
                delay(200);  // 给硬件足够时间放电
                
                // 2. 手动拉低RESET引脚500ms，彻底复位摄像头传感器
                pinMode(CAM_PIN_RESET, OUTPUT);
                digitalWrite(CAM_PIN_RESET, LOW);
                delay(200);
                digitalWrite(CAM_PIN_RESET, HIGH);
                delay(200);  // 等待传感器稳定
                
                // 3. 重新初始化I2C总线（有时SCCB会锁死）
                Wire.end();  // 先关闭
                delay(100);
                Wire.begin(CAM_PIN_SIOD, CAM_PIN_SIOC);
                
                // 4. 重新初始化摄像头
                if (!camera_driver_init()) {
                    Serial.println("摄像头重新初始化仍失败，将在下一次循环重试");
                    // 不清除failCount，让它继续累加，达到一定次数后再尝试
                    // 但为了避免无限循环，可以等待更长时间再试
                    failCount = 0;  // 清零，重新计数
                    lastUpload = millis();  // 延迟下一次拍照
                } else {
                    Serial.println("摄像头复位成功");
                    failCount = 0;
                    lastUpload = millis();
                }
            }
        }
    }
    delay(100);
}

// 立即抓拍并上传一次（不受 uploadInterval 限速），供拍照按钮 / MQTT 命令调用
bool camera_system_capture_now() {
    camera_fb_t* fb = camera_capture();
    if (!fb) {
        Serial.println("立即抓拍失败");
        return false;
    }
    bool ok = camera_upload_photo(fb);
    esp_camera_fb_return(fb);
    return ok;
}

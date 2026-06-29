#ifndef CAMERA_H
#define CAMERA_H

// ==================== 摄像头引脚定义 ====================
#define CAM_PIN_PWDN   10
#define CAM_PIN_RESET  47
#define CAM_PIN_XCLK   11
#define CAM_PIN_SIOD   39
#define CAM_PIN_SIOC   38

#define CAM_PIN_D7     21
#define CAM_PIN_D6     13
#define CAM_PIN_D5     12
#define CAM_PIN_D4     15
#define CAM_PIN_D3     7
#define CAM_PIN_D2     6
#define CAM_PIN_D1     5
#define CAM_PIN_D0     4

#define CAM_PIN_VSYNC  14
#define CAM_PIN_HREF   3
#define CAM_PIN_PCLK   45

// 初始化摄像头系统（依赖 WiFi 已连接）
void camera_system_init();

// 循环拍照并上传，需在主 loop 中反复调用（受 uploadInterval 限速）
void camera_system_loop();

// 立即抓拍并上传一次（不受限速影响），供拍照按钮 / 命令调用
bool camera_system_capture_now();

#endif
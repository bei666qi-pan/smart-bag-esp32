#ifndef MQTT_H
#define MQTT_H

// MQTT Topic（严格按文档）
#define TOPIC_STATUS    "v5/bag/status"
#define TOPIC_SENSORS   "v5/bag/sensors"
#define TOPIC_GPS       "v5/bag/gps"
#define TOPIC_CMD       "v5/bag/cmd"
#define TOPIC_CMD_ACK   "v5/bag/cmd/ack"
#define TOPIC_VOICE_CMD "v5/bag/voice/cmd"  // [语音联动] Jetson→ESP32 控制下行（单向，无 id，不回 ACK）
#define TOPIC_VOICE_HEALTH "v5/bag/voice/health"  // [就近告警] Jetson 麦克风健康态(retain)，订阅后本机屏幕/LED 现场提示
#define TOPIC_VIDEO     "v5/bag/video"      // 新增：视频数据上报主题
// 上报间隔（秒）
#define INTERVAL 10

void mqtt_loop();                                             //MQTT心跳包
void callback(char* topic, byte* payload, unsigned int len);  // MQTT 回调(接收命令)
void mqtt_reconnect();                                        //MQTT 重连（带LWT）
void mqtt_publish_sensors(float temp,float humid);            //上报温湿度传感器数据
void mqtt_publish_gps(float lat, float lng);                  //上报GPS数据
void MQTT_set();                                              //初始化

#endif

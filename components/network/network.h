#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>
#include "sensors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 网络模块初始化
 * 
 * 初始化WiFi和MQTT客户端：
 * - WiFi: 连接到 "Tim" 网络
 * - MQTT: 连接到 mqtt://172.20.10.6:1883
 * 
 * 该函数会：
 * 1. 初始化NVS存储（WiFi驱动需要）
 * 2. 初始化网络接口和事件循环
 * 3. 配置WiFi为STA模式
 * 4. 启动MQTT客户端并注册事件处理
 */
void network_init(void);

/**
 * @brief 检查网络是否就绪（WiFi + MQTT均已连接）
 * 
 * @return true 网络已就绪，可以发送数据
 * @return false 网络未就绪
 */
bool network_is_connected(void);

/**
 * @brief 发布传感器数据到MQTT服务器
 * 
 * @param data 指向传感器数据结构体的指针
 * 
 * 将传感器数据（温度、pH、溶解氧、盐度）转换为JSON格式，
 * 发布到MQTT主题 "sensor/water"，格式为：
 * {"temperature":xxx.xx,"ph":xxx.xx,"oxygen":xxx.xx,"salinity":xxx.xx}
 * 
 * 如果网络未就绪，会跳过本次发布并打印警告日志。
 */
void network_publish_sensor_data(const sensor_data_t* data);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_H

#include "network.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <stdio.h>

#define WIFI_SSID "Tim"
#define WIFI_PASS "12345678"

// 日志标签
static const char* TAG = "network";

// 连接状态标志位
#define WIFI_CONNECTED_BIT  BIT0
#define MQTT_CONNECTED_BIT  BIT1
static EventGroupHandle_t network_event_group;

// MQTT客户端句柄
static esp_mqtt_client_handle_t mqtt_client;

// MQTT服务器地址
static const char* mqtt_uri = "mqtt://172.20.10.6:1883";

// MQTT发布主题（传感器数据上报）
static const char* mqtt_topic = "sensor/water";

// 轻量离线缓存：仅保留最近一条未发送数据，避免断网时持续丢数据。
static sensor_data_t s_pending_data;
static bool s_has_pending = false;

// 发布数据到MQTT服务器，返回是否成功
static bool publish_one(const sensor_data_t *data, const char *source)
{
    char payload[256];
    int len = snprintf(payload, sizeof(payload),
                       "{\"temperature\":%.2f,\"ph\":%.2f,\"oxygen\":%.2f,\"salinity\":%.2f}",
                       data->temperature_c, data->ph, data->dissolved_oxygen, data->salinity);

    if (len <= 0 || len >= (int)sizeof(payload)) {
        ESP_LOGE(TAG, "Payload format failed (source=%s)", source);
        return false;
    }

    int msg_id = esp_mqtt_client_publish(mqtt_client, mqtt_topic, payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Publish failed (source=%s)", source);
        return false;
    }

    ESP_LOGI(TAG, "Published (%s): %s", source, payload);
    return true;
}

// 将当前数据存储为待发送的最新数据（覆盖之前的缓存），并记录原因。
static void store_pending_latest(const sensor_data_t *data, const char *reason)
{
    s_pending_data = *data;
    s_has_pending = true;
    ESP_LOGW(TAG, "Store latest sample for retry (%s)", reason);
}

/**
 * @brief WiFi事件处理函数
 * 
 * 处理WiFi相关事件：
 * - WIFI_EVENT_STA_START: WiFi启动，开始连接
 * - WIFI_EVENT_STA_DISCONNECTED: WiFi断开连接，自动重连
 * - IP_EVENT_STA_GOT_IP: 成功获取IP地址
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // WiFi启动完成，开始连接到目标网络
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disc = (const wifi_event_sta_disconnected_t *)event_data;
        // 清除连接标志。WiFi断开时，MQTT也视为不可用。
        xEventGroupClearBits(network_event_group, WIFI_CONNECTED_BIT | MQTT_CONNECTED_BIT);
        ESP_LOGW(TAG, "WiFi disconnected (reason=%d), reconnecting...",
                 disc ? (int)disc->reason : -1);
        // 避免在事件回调中阻塞，直接触发重连。
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        // 成功获取IP地址，设置WiFi连接标志
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(network_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief MQTT事件处理函数
 * 
 * 处理MQTT连接相关事件：
 * - MQTT_EVENT_CONNECTED: MQTT连接成功
 * - MQTT_EVENT_DISCONNECTED: MQTT连接断开
 */
static void mqtt_event_handler_cb(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data)
{
    esp_mqtt_event_t* event = (esp_mqtt_event_t*)event_data;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            // MQTT连接成功，设置标志位
            ESP_LOGI(TAG, "MQTT connected");
            xEventGroupSetBits(network_event_group, MQTT_CONNECTED_BIT);
            break;
        case MQTT_EVENT_DISCONNECTED:
            // MQTT连接断开，清除标志位（MQTT库会自动重连）
            ESP_LOGW(TAG, "MQTT disconnected");
            xEventGroupClearBits(network_event_group, MQTT_CONNECTED_BIT);
            break;
        default:
            break;
    }
}

void network_init(void)
{
    // 创建事件组，用于追踪WiFi和MQTT的连接状态
    network_event_group = xEventGroupCreate();

    // 步骤1: 初始化NVS存储
    // NVS（非易失性存储）用于保存WiFi驱动需要的配置信息（如WiFi密码等）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS分区已满或版本不匹配，擦除后重新初始化
        ESP_LOGW(TAG, "NVS issue, erasing and reinitializing...");
        nvs_flash_erase();
        nvs_flash_init();
    }

    // 步骤2: 初始化网络接口
    // 创建TCP/IP网络栈
    esp_netif_init();
    
    // 创建默认事件循环，用于处理WiFi和IP事件
    esp_event_loop_create_default();
    
    // 创建WiFi STA（站点）模式的网络接口
    esp_netif_create_default_wifi_sta();

    // 步骤3: WiFi初始化和配置
    // 使用默认配置初始化WiFi驱动
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    // 注册WiFi事件处理器
    // WIFI_EVENT: 处理WiFi事件（连接、断开等）
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    // IP_EVENT: 处理IP事件（获得IP地址）
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    // 配置WiFi网络信息
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,              // WiFi网络名称
            .password = WIFI_PASS,    // WiFi密码
        },
    };

    // 设置WiFi为STA（客户端）模式
    esp_wifi_set_mode(WIFI_MODE_STA);
    
    // 应用WiFi配置
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    
    // 启动WiFi（开始连接到目标网络）
    esp_wifi_start();

    // 步骤4: MQTT客户端初始化
    // 配置MQTT连接参数
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = mqtt_uri,  // MQTT服务器地址
            },
        },
    };
    
    // 根据配置初始化MQTT客户端
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    
    // 注册MQTT事件处理器
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler_cb, NULL);
    
    // 启动MQTT客户端（开始连接到MQTT服务器）
    esp_mqtt_client_start(mqtt_client);

    ESP_LOGI(TAG, "Network init complete");
}

bool network_is_connected(void)
{
    // 检查WiFi和MQTT是否都已连接
    EventBits_t bits = xEventGroupGetBits(network_event_group);
    return (bits & WIFI_CONNECTED_BIT) && (bits & MQTT_CONNECTED_BIT);
}

void network_publish_sensor_data(const sensor_data_t* data)
{
    // 检查参数和MQTT客户端是否有效
    if (!data || !mqtt_client) return;

    // 检查网络连接状态
    if (!network_is_connected()) {
        store_pending_latest(data, "offline");
        return;
    }

    // 为了不增加负载，每个周期最多只发一条。
    // 若有缓存，优先补发缓存；补发成功后本周期不再发送最新值。
    if (s_has_pending) {
        if (publish_one(&s_pending_data, "retry")) {
            s_has_pending = false;
            return;
        }
        store_pending_latest(data, "retry-failed-update-latest");
        return;
    }

    if (!publish_one(data, "live")) {
        store_pending_latest(data, "live-publish-failed");
    }
}

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "gpio_config.h"
#include "sensors.h"
#include "actuator.h"
#include "rgb_led.h"
#include "buzzer.h"
#include "network.h"

static const char *TAG = "jellyfish";

#define EVT_SENSOR_READY BIT0

typedef enum {
    SYS_NORMAL = 0,
    SYS_ALARM_TEMP,
    SYS_ALARM_WATER,
    SYS_ALARM_BOTH,
} system_state_t;

static EventGroupHandle_t s_evt = NULL;
static sensor_data_t s_sensor_data = {0};
static system_state_t s_state = SYS_NORMAL;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_manual_water_mode = false;  // 手动换水模式标志：true表示按钮控制泵，false表示自动控制

static const char *state_to_string(system_state_t state)
{
    switch (state) {
    case SYS_ALARM_TEMP:
        return "ALARM_TEMP";
    case SYS_ALARM_WATER:
        return "ALARM_WATER";
    case SYS_ALARM_BOTH:
        return "ALARM_BOTH";
    case SYS_NORMAL:
    default:
        return "NORMAL";
    }
}

static void set_sensor_data(const sensor_data_t *data)
{
    portENTER_CRITICAL(&s_lock);
    s_sensor_data = *data;
    portEXIT_CRITICAL(&s_lock);
}

static sensor_data_t get_sensor_data(void)
{
    sensor_data_t data;
    portENTER_CRITICAL(&s_lock);
    data = s_sensor_data;
    portEXIT_CRITICAL(&s_lock);
    return data;
}

static void set_system_state(system_state_t state)
{
    portENTER_CRITICAL(&s_lock);
    s_state = state;
    portEXIT_CRITICAL(&s_lock);
}

static system_state_t get_system_state(void)
{
    system_state_t state;
    portENTER_CRITICAL(&s_lock);
    state = s_state;
    portEXIT_CRITICAL(&s_lock);
    return state;
}

static void task_sensor_read(void *arg)
{
    (void)arg;

    while (true) {
        sensor_data_t data = sensors_read_all();
        set_sensor_data(&data);
        xEventGroupSetBits(s_evt, EVT_SENSOR_READY);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void task_control_logic(void *arg)
{
    (void)arg;
    system_state_t last_state = SYS_NORMAL;

    while (true) {
        xEventGroupWaitBits(s_evt, EVT_SENSOR_READY, pdTRUE, pdFALSE, portMAX_DELAY);

        sensor_data_t data = get_sensor_data();
        bool temp_bad = sensors_is_temperature_out_of_range(data.temperature_c);
        bool water_bad = sensors_needs_water_change(&data);

        // ── 状态机 ────────────────────────────
        system_state_t next_state;
        if (water_bad && temp_bad) {
            next_state = SYS_ALARM_BOTH;
        } else if (water_bad) {
            next_state = SYS_ALARM_WATER;
        } else if (temp_bad) {
            next_state = SYS_ALARM_TEMP;
        } else {
            next_state = SYS_NORMAL;
        }

        // ── 执行器控制 ─────────────────────────
        o2_pump_control_smart(data.do_saturation);
        heater_control_smart(data.temperature_c);
        water_pump_control_smart(data.ph, data.salinity);

        buzzer_set(temp_bad || water_bad);

        set_system_state(next_state);

        if (next_state != last_state) {
            ESP_LOGW(TAG, "State changed: %s -> %s", state_to_string(last_state), state_to_string(next_state));
            last_state = next_state;
        }

        ESP_LOGI(TAG,
                 "T=%.1fC pH=%.2f DO=%.0f%% Sal=%.1f | state=%s pump=%d heater=%d o2=%d buzzer=%d",
                 data.temperature_c, data.ph,
                 data.do_saturation * 100.0f, data.salinity,
                 state_to_string(next_state),
                 water_pump_get_state(), heater_get_state(), o2_pump_get_state(),
                 temp_bad || water_bad);
    }
}

static void task_network_publish(void *arg)
{
    (void)arg;

    while (true) {
        sensor_data_t data = get_sensor_data();
        network_publish_sensor_data(&data);
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}

static void task_button_handler(void *arg)
{
    (void)arg;
    bool button_pressed_prev = false;
    
    // 等待系统稳定，避免启动时的GPIO噪声误触发
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 初始化上一次的按钮状态
    button_pressed_prev = (gpio_get_level(GPIO_WATER_CHANGE_BTN) == 0);
    
    ESP_LOGI(TAG, "Button handler task started");
    
    while (true) {
        int button_level = gpio_get_level(GPIO_WATER_CHANGE_BTN);
        bool button_pressed_now = (button_level == 0);  // 上拉电阻，按下时为低电平
        
        // 去抖动：检查按钮是否从未按下变为按下
        if (button_pressed_now && !button_pressed_prev) {
            // 按钮刚刚被按下
            // 等待50ms确认按下
            vTaskDelay(pdMS_TO_TICKS(50));
            
            int button_level_confirmed = gpio_get_level(GPIO_WATER_CHANGE_BTN);
            if (button_level_confirmed == 0) {
                // 确认按下有效
                s_manual_water_mode = !s_manual_water_mode;  // 切换模式
                
                if (s_manual_water_mode) {
                    // 进入手动模式：启动泵
                    pump_control(1, actuator_on);
                    pump_control(2, actuator_on);
                    ESP_LOGI(TAG, "Manual water mode ON - pumps started");
                } else {
                    // 退出手动模式：停止泵
                    pump_control(1, actuator_off);
                    pump_control(2, actuator_off);
                    ESP_LOGI(TAG, "Manual water mode OFF - pumps stopped, auto-control resumed");
                }
                
                // 等待按钮松开，防止连续触发
                while (gpio_get_level(GPIO_WATER_CHANGE_BTN) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                vTaskDelay(pdMS_TO_TICKS(100));  // 额外延迟防止抖动
            }
        }
        
        button_pressed_prev = button_pressed_now;
        vTaskDelay(pdMS_TO_TICKS(50));  // 50ms轮询间隔
    }
}

static void task_led_effect(void *arg)
{
    (void)arg;
    rgb_led_display_state(false);

    while (true) {
        system_state_t state = get_system_state();
        rgb_led_display_state(state != SYS_NORMAL);
        rgb_led_update();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== JELLYFISH NORMAL RUN MODE (NO AI) ===");

    gpio_config_init();
    sensors_init();
    rgb_led_init();
    buzzer_init();
    network_init();

    // Normal baseline: O2 pump always on, others off.
    // 注释掉以解决 brownout（功率不足）问题
    o2_pump_set(true);
    circ_pump_pwm_init();
    circ_pump_set_speed(50);
    heater_set(false);
    pump_control(1, actuator_off);
    pump_control(2, actuator_off);
    buzzer_set(false);
    // rgb_led_set_color(0, 255, 0);
    rgb_led_clear();

    s_evt = xEventGroupCreate();

    xTaskCreate(task_sensor_read, "task_sensor_read", 4096, NULL, 5, NULL);
    xTaskCreate(task_control_logic, "task_control_logic", 4096, NULL, 5, NULL);
    xTaskCreate(task_network_publish, "task_network_publish", 4096, NULL, 3, NULL);
    xTaskCreate(task_button_handler, "task_button_handler", 4096, NULL, 4, NULL);
    xTaskCreate(task_led_effect, "task_led_effect", 3072, NULL, 2, NULL);

    ESP_LOGI(TAG, "Tasks started: sensor/control/network/button/led");
}
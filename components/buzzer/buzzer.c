#include "buzzer.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "buzzer";

// GPIO pin for buzzer
#define GPIO_BUZZER 10

void buzzer_init(void)
{
    // Configure GPIO_BUZZER as output
    gpio_config_t buzzer_cfg = {
        .pin_bit_mask = (1ULL << GPIO_BUZZER),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&buzzer_cfg);
    
    // Default: buzzer off
    gpio_set_level(GPIO_BUZZER, 0);
    ESP_LOGI(TAG, "Buzzer initialized on GPIO %d", GPIO_BUZZER);
}

void buzzer_set(bool enabled)
{
    gpio_set_level(GPIO_BUZZER, enabled ? 1 : 0);
    ESP_LOGI(TAG, "Buzzer %s", enabled ? "ON" : "OFF");
}

void buzzer_beep(uint8_t count, uint16_t duration_ms, uint16_t interval_ms)
{
    ESP_LOGI(TAG, "Playing %d beeps (duration=%dms, interval=%dms)", count, duration_ms, interval_ms);
    
    for (uint8_t i = 0; i < count; i++) {
        buzzer_set(true);
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        
        buzzer_set(false);
        if (i < count - 1) {
            vTaskDelay(pdMS_TO_TICKS(interval_ms));
        }
    }
}

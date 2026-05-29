#include "gpio_config.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_err.h"
#include "esp_log.h"

void gpio_config_init(void)
{
    // Actuator pins as output (N-MOSFET)
    // 这些引脚连接 N-MOSFET 驱动模块，低电平时关闭， 高电平时开启
    gpio_config_t pump_conf = {
        .pin_bit_mask = (1ULL<<GPIO_PUMP_1) | (1ULL<<GPIO_PUMP_2)
                      | (1ULL<<GPIO_O2_PUMP) | (1ULL<<GPIO_HEATER),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pump_conf);

    // RS485 DE/RE direction control
    gpio_config_t rs485_conf = {
        .pin_bit_mask = (1ULL<<GPIO_RS485_DE_RE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rs485_conf);

    // WS2812 data pin (RMT控制由led模块接管，先配为OUTPUT兜底)
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL<<GPIO_RGB_DATA),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);

    // Manual water change button input (GPIO_WATER_CHANGE_BTN)
    // 带上拉电阻，按下时被拉低
    gpio_config_t button_conf = {
        .pin_bit_mask = (1ULL<<GPIO_WATER_CHANGE_BTN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&button_conf);

    // ADC 初始化
    // 12位 ADC 会产生 0-4095 之间的原始值。
    // 12dB 衰减允许测量 0-3.3V 输入。
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC_PH_CHANNEL, ADC_ATTEN_DB_12);

    // 默认关机/安全状态：所有泵关闭，RS485 进入接收
    // 蜂鸣器由buzzer模块独立管理
    gpio_set_level(GPIO_PUMP_1, 0);
    gpio_set_level(GPIO_PUMP_2, 0);
    gpio_set_level(GPIO_O2_PUMP, 0);
    gpio_set_level(GPIO_HEATER, 0);
    gpio_set_level(GPIO_RS485_DE_RE, 0);
    ESP_LOGI("GPIO", "GPIO pins initialized");
}

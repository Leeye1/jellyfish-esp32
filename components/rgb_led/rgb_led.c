#include "rgb_led.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "gpio_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static led_strip_handle_t led_strip;
static const char* TAG = "rgb_led";

// ── 深海呼吸·蓝白渐变参数 ────────────────────
#define DEEP_BLUE_R   0
#define DEEP_BLUE_G   0
#define DEEP_BLUE_B   60
#define MOON_WHITE_R  80
#define MOON_WHITE_G  95
#define MOON_WHITE_B  110
#define BREATH_HALF_CYCLE_MS 10000
#define BLINK_PERIOD_MS      500

// ── 五彩渐变·颜色参数 ────────────────────────
#define COLORFUL_GRADIENT_MS 60000  // 完整循环时间 60秒（每种颜色渐变10秒）
typedef struct {
    uint8_t r, g, b;
} color_t;

// 五彩渐变的颜色序列（红→黄→绿→青→蓝→紫→红）
static const color_t colorful_colors[] = {
    {255, 0, 0},    // 红色
    {255, 255, 0},  // 黄色
    {0, 255, 0},    // 绿色
    {0, 255, 255},  // 青色
    {0, 0, 255},    // 蓝色
    {255, 0, 255},  // 紫色
};
#define COLORFUL_COLOR_COUNT 6

// ── 平滑渐变状态管理 ──────────────────────────
typedef struct {
    uint8_t current_r, current_g, current_b;
    uint8_t start_r, start_g, start_b;
    uint8_t target_r, target_g, target_b;
    uint64_t start_time_us;
    uint32_t duration_ms;
    bool is_animating;
    bool is_colorful_mode;  // 标志是否处于彩色渐变模式
    int current_color_index;  // 当前颜色在彩色序列中的索引
} animation_state_t;

static animation_state_t s_animation = {0};

// ── 显示模式状态 ─────────────────────────────
static bool s_alarm_mode = true;
static bool s_breath_direction = true;  // true=向上(深蓝→月白), false=向下
static int64_t s_last_blink_us = 0;
static bool s_blink_on = false;

void rgb_led_init(void)
{
    // 初始化 30 个 WS2812 LED
    led_strip_config_t strip_config = {
        .strip_gpio_num = GPIO_RGB_DATA,
        .max_leds = RGB_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .mem_block_symbols = 0,
        .flags = {
            .with_dma = false,
        },
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LED strip init failed: %s", esp_err_to_name(err));
        return;
    }

    led_strip_clear(led_strip);
    s_animation.current_r = DEEP_BLUE_R;
    s_animation.current_g = DEEP_BLUE_G;
    s_animation.current_b = DEEP_BLUE_B;
    for (int i = 0; i < RGB_LED_COUNT; i++) {
        led_strip_set_pixel(led_strip, i, DEEP_BLUE_R, DEEP_BLUE_G, DEEP_BLUE_B);
    }
    led_strip_refresh(led_strip);
    ESP_LOGI(TAG, "RGB LED strip initialized with %d pixels", RGB_LED_COUNT);
}

void rgb_led_set_color(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!led_strip) return;
    
    // 立即停止动画，设置固定颜色
    s_animation.is_animating = false;
    s_animation.current_r = red;
    s_animation.current_g = green;
    s_animation.current_b = blue;
    s_animation.start_r = red;
    s_animation.start_g = green;
    s_animation.start_b = blue;
    s_animation.target_r = red;
    s_animation.target_g = green;
    s_animation.target_b = blue;
    
    // Set all pixels to the same color
    for (int i = 0; i < RGB_LED_COUNT; i++) {
        led_strip_set_pixel(led_strip, i, red, green, blue);
    }
    led_strip_refresh(led_strip);
}

void rgb_led_set_pixel(uint8_t index, uint8_t red, uint8_t green, uint8_t blue)
{
    if (!led_strip || index >= RGB_LED_COUNT) return;
    led_strip_set_pixel(led_strip, index, red, green, blue);
}

void rgb_led_clear(void)
{
    if (!led_strip) return;
    s_animation.is_animating = false;
    s_animation.current_r = 0;
    s_animation.current_g = 0;
    s_animation.current_b = 0;
    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);
}

void rgb_led_refresh(void)
{
    if (!led_strip) return;
    led_strip_refresh(led_strip);
}

void rgb_led_display_state(bool is_alarm)
{
    if (is_alarm == s_alarm_mode) return;

    s_alarm_mode = is_alarm;
    s_animation.is_animating = false;

    if (is_alarm) {
        rgb_led_set_color(255, 0, 0);
        s_last_blink_us = esp_timer_get_time();
        s_blink_on = true;
    } else {
        s_breath_direction = true;
        rgb_led_set_color(DEEP_BLUE_R, DEEP_BLUE_G, DEEP_BLUE_B);
        rgb_led_set_color_smooth(MOON_WHITE_R, MOON_WHITE_G, MOON_WHITE_B, BREATH_HALF_CYCLE_MS);
    }
}

void rgb_led_set_color_smooth(uint8_t target_r, uint8_t target_g, uint8_t target_b, uint32_t duration_ms)
{
    if (!led_strip || duration_ms == 0) return;
    
    s_animation.is_colorful_mode = false;  // 退出彩色模式
    
    // 如果正在进行渐变，先完成插值到当前值，作为新渐变的起点
    if (s_animation.is_animating) {
        uint64_t current_time_us = esp_timer_get_time();
        uint64_t elapsed_us = current_time_us - s_animation.start_time_us;
        uint32_t elapsed_ms = elapsed_us / 1000;
        
        if (elapsed_ms < s_animation.duration_ms) {
            // 计算当前进度，完成插值
            float progress = (float)elapsed_ms / (float)s_animation.duration_ms;
            s_animation.current_r = (uint8_t)(s_animation.start_r + 
                                    (s_animation.target_r - s_animation.start_r) * progress + 0.5f);
            s_animation.current_g = (uint8_t)(s_animation.start_g + 
                                    (s_animation.target_g - s_animation.start_g) * progress + 0.5f);
            s_animation.current_b = (uint8_t)(s_animation.start_b + 
                                    (s_animation.target_b - s_animation.start_b) * progress + 0.5f);
        } else {
            // 渐变已完成，直接设置为目标颜色
            s_animation.current_r = s_animation.target_r;
            s_animation.current_g = s_animation.target_g;
            s_animation.current_b = s_animation.target_b;
        }
    }
    
    // 启动新的渐变，保存起始颜色
    s_animation.start_r = s_animation.current_r;
    s_animation.start_g = s_animation.current_g;
    s_animation.start_b = s_animation.current_b;
    s_animation.target_r = target_r;
    s_animation.target_g = target_g;
    s_animation.target_b = target_b;
    s_animation.start_time_us = esp_timer_get_time();
    s_animation.duration_ms = duration_ms;
    s_animation.is_animating = true;
    
    ESP_LOGD(TAG, "Smooth color transition started: RGB(%d,%d,%d) -> RGB(%d,%d,%d) over %dms",
             s_animation.start_r, s_animation.start_g, s_animation.start_b,
             target_r, target_g, target_b, duration_ms);
}

/**
 * @brief 启动五彩斑斓渐变效果
 * 
 * 颜色序列：红 → 黄 → 绿 → 青 → 蓝 → 紫 → 红
 * 完整循环时间为COLORFUL_GRADIENT_MS（2.5秒）
 */
void rgb_led_colorful_gradient(void)
{
    if (!led_strip) return;
    
    s_alarm_mode = false;  // 退出告警模式，启用彩色渐变
    s_animation.is_colorful_mode = true;
    s_animation.is_animating = true;
    s_animation.current_color_index = 0;
    s_animation.start_time_us = esp_timer_get_time();
    
    // 设置初始颜色为红色
    color_t start_color = colorful_colors[0];
    s_animation.current_r = start_color.r;
    s_animation.current_g = start_color.g;
    s_animation.current_b = start_color.b;
    s_animation.start_r = start_color.r;
    s_animation.start_g = start_color.g;
    s_animation.start_b = start_color.b;
    
    // 计算每种颜色的持续时间
    uint32_t color_duration_ms = COLORFUL_GRADIENT_MS / COLORFUL_COLOR_COUNT;
    s_animation.duration_ms = color_duration_ms;
    
    // 设置目标为第一个颜色的下一个颜色
    color_t target_color = colorful_colors[1];
    s_animation.target_r = target_color.r;
    s_animation.target_g = target_color.g;
    s_animation.target_b = target_color.b;
    
    ESP_LOGI(TAG, "Colorful gradient started (60s cycle, 10s per color)");
}

void rgb_led_update(void)
{
    if (!led_strip) return;

    if (s_alarm_mode) {
        int64_t now = esp_timer_get_time();
        if ((now - s_last_blink_us) >= BLINK_PERIOD_MS * 1000) {
            s_last_blink_us = now;
            s_blink_on = !s_blink_on;
            rgb_led_set_color(s_blink_on ? 255 : 0, 0, 0);
        }
        return;
    }

    // 彩色渐变模式处理
    if (s_animation.is_colorful_mode && s_animation.is_animating) {
        uint64_t current_time_us = esp_timer_get_time();
        uint64_t elapsed_us = current_time_us - s_animation.start_time_us;
        uint32_t elapsed_ms = elapsed_us / 1000;

        // 计算在整个循环中的位置
        uint32_t cycle_position_ms = elapsed_ms % COLORFUL_GRADIENT_MS;
        
        // 计算当前颜色段和进度
        uint32_t color_duration_ms = COLORFUL_GRADIENT_MS / COLORFUL_COLOR_COUNT;
        int current_segment = cycle_position_ms / color_duration_ms;
        int next_segment = (current_segment + 1) % COLORFUL_COLOR_COUNT;
        uint32_t segment_elapsed_ms = cycle_position_ms % color_duration_ms;
        
        // 获取当前和下一个颜色
        color_t current_color = colorful_colors[current_segment];
        color_t next_color = colorful_colors[next_segment];
        
        // 计算当前颜色段的进度（0.0 - 1.0）
        float progress = (float)segment_elapsed_ms / (float)color_duration_ms;
        
        // 线性插值计算当前RGB值
        s_animation.current_r = (uint8_t)(current_color.r + 
                                (next_color.r - current_color.r) * progress + 0.5f);
        s_animation.current_g = (uint8_t)(current_color.g + 
                                (next_color.g - current_color.g) * progress + 0.5f);
        s_animation.current_b = (uint8_t)(current_color.b + 
                                (next_color.b - current_color.b) * progress + 0.5f);
        
        // 更新所有LED像素
        for (int i = 0; i < RGB_LED_COUNT; i++) {
            led_strip_set_pixel(led_strip, i, s_animation.current_r, s_animation.current_g, s_animation.current_b);
        }
        led_strip_refresh(led_strip);
        return;
    }

    if (!s_animation.is_animating) return;

    uint64_t current_time_us = esp_timer_get_time();
    uint64_t elapsed_us = current_time_us - s_animation.start_time_us;
    uint32_t elapsed_ms = elapsed_us / 1000;

    if (elapsed_ms >= s_animation.duration_ms) {
        s_animation.current_r = s_animation.target_r;
        s_animation.current_g = s_animation.target_g;
        s_animation.current_b = s_animation.target_b;

        // 呼吸模式：自动翻转方向，启动下一阶段
        s_breath_direction = !s_breath_direction;
        if (s_breath_direction) {
            rgb_led_set_color_smooth(MOON_WHITE_R, MOON_WHITE_G, MOON_WHITE_B, BREATH_HALF_CYCLE_MS);
        } else {
            rgb_led_set_color_smooth(DEEP_BLUE_R, DEEP_BLUE_G, DEEP_BLUE_B, BREATH_HALF_CYCLE_MS);
        }
    } else {
        float progress = (float)elapsed_ms / (float)s_animation.duration_ms;

        s_animation.current_r = (uint8_t)(s_animation.start_r +
                                (s_animation.target_r - s_animation.start_r) * progress + 0.5f);
        s_animation.current_g = (uint8_t)(s_animation.start_g +
                                (s_animation.target_g - s_animation.start_g) * progress + 0.5f);
        s_animation.current_b = (uint8_t)(s_animation.start_b +
                                (s_animation.target_b - s_animation.start_b) * progress + 0.5f);
    }

    for (int i = 0; i < RGB_LED_COUNT; i++) {
        led_strip_set_pixel(led_strip, i, s_animation.current_r, s_animation.current_g, s_animation.current_b);
    }
    led_strip_refresh(led_strip);
}

#include "actuator.h"
#include "gpio_config.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "sensors.h"

static const char* TAG = "ACTUATOR";

/* ── 内循环泵 PWM 参数 ───────────────────────── */
#define CIRC_PUMP_LEDC_TIMER     LEDC_TIMER_0
#define CIRC_PUMP_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define CIRC_PUMP_LEDC_CHANNEL   LEDC_CHANNEL_0
#define CIRC_PUMP_LEDC_DUTY_RES  LEDC_TIMER_10_BIT
#define CIRC_PUMP_PWM_FREQ_HZ    1000
#define CIRC_PUMP_MAX_DUTY       1023

/* ── 执行器状态记录（用于滞后控制） ──────────────── */
static bool s_o2_pump_state    = false;
static bool s_heater_state     = false;
static bool s_water_pump_state = false;
static uint8_t s_circ_pump_speed = 0;

/* ================================================================
 * 底层控制函数
 * ================================================================ */

void actuator_control(uint8_t id, actuator_state_t state)
{
    int pin;
    const char *name;
    switch (id) {
        case 1: pin = GPIO_PUMP_1;  name = "Water Pump 1"; break;
        case 2: pin = GPIO_PUMP_2;  name = "Water Pump 2"; break;
        case 3: pin = GPIO_O2_PUMP; name = "O2 Pump";      break;
        case 4: pin = GPIO_HEATER;    name = "Heater";       break;
        case 5: circ_pump_set_speed(state ? 100 : 0); return;
        default:
            ESP_LOGE(TAG, "Invalid actuator ID: %d", id);
            return;
    }

    int level = (state == actuator_on) ? 1 : 0;

    esp_err_t ret = gpio_set_level(pin, level);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO%d level: %s", pin, esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "✓ %s (ID=%d): GPIO%d set to %d", name, id, pin, level);
}

void pump_control(uint8_t pump_id, actuator_state_t state)
{
    actuator_control(pump_id, state);
}

void o2_pump_set(bool on)
{
    actuator_control(3, on ? actuator_on : actuator_off);
}

void heater_set(bool on)
{
    actuator_control(4, on ? actuator_on : actuator_off);
}

/* ── 内循环泵 PWM 实现 ──────────────────────── */

void circ_pump_pwm_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode       = CIRC_PUMP_LEDC_MODE,
        .duty_resolution  = CIRC_PUMP_LEDC_DUTY_RES,
        .timer_num        = CIRC_PUMP_LEDC_TIMER,
        .freq_hz          = CIRC_PUMP_PWM_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t ch_conf = {
        .gpio_num       = GPIO_CIRC_PUMP,
        .speed_mode     = CIRC_PUMP_LEDC_MODE,
        .channel        = CIRC_PUMP_LEDC_CHANNEL,
        .timer_sel      = CIRC_PUMP_LEDC_TIMER,
        .duty           = 0,
        .hpoint         = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_conf));

    ESP_LOGI(TAG, "Circ pump PWM init: GPIO%d, %dHz, %d-bit",
             GPIO_CIRC_PUMP, CIRC_PUMP_PWM_FREQ_HZ,
             CIRC_PUMP_LEDC_DUTY_RES == LEDC_TIMER_10_BIT ? 10 : 8);
}

void circ_pump_set_speed(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }

    uint32_t duty = (uint32_t)percent * CIRC_PUMP_MAX_DUTY / 100;

    ledc_set_duty(CIRC_PUMP_LEDC_MODE, CIRC_PUMP_LEDC_CHANNEL, duty);
    ledc_update_duty(CIRC_PUMP_LEDC_MODE, CIRC_PUMP_LEDC_CHANNEL);

    s_circ_pump_speed = percent;
    ESP_LOGI(TAG, "Circ pump PWM set: %d%% (duty=%ld/%d)",
             percent, duty, CIRC_PUMP_MAX_DUTY);
}

/* ================================================================
 * O2 泵智能控制（滞后控制）
 * ================================================================
 *   DO <= 75%：开启泵
 *   DO >= 99%：关闭泵
 *   75% < DO < 99%：保持当前状态
 */
void o2_pump_control_smart(float do_saturation)
{
    if (do_saturation <= DO_SAT_MIN) {
        if (!s_o2_pump_state) {
            s_o2_pump_state = true;
            o2_pump_set(true);
            ESP_LOGI(TAG, "O2 pump ON: DO=%.0f%% <= %.0f%% threshold",
                     do_saturation * 100.0f, DO_SAT_MIN * 100.0f);
        }
    } else if (do_saturation >= DO_SAT_MAX) {
        if (s_o2_pump_state) {
            s_o2_pump_state = false;
            o2_pump_set(false);
            ESP_LOGI(TAG, "O2 pump OFF: DO=%.0f%% >= %.0f%% threshold",
                     do_saturation * 100.0f, DO_SAT_MAX * 100.0f);
        }
    }
    /* 否则保持当前状态 */
}

/* ================================================================
 * 加热棒智能控制（滞后控制）
 * ================================================================
 *   T <= 22°C (TEMP_MIN)：开启加热
 *   T >= 28°C (TEMP_MAX)：关闭加热（绝不在高温时开启）
 *   22°C < T < 28°C：保持当前状态
 */
void heater_control_smart(float temperature_c)
{
    if (temperature_c <= TEMP_MIN) {
        if (!s_heater_state) {
            s_heater_state = true;
            heater_set(true);
            ESP_LOGI(TAG, "Heater ON: T=%.1f°C <= %.0f°C threshold",
                     temperature_c, TEMP_MIN);
        }
    } else if (temperature_c >= TEMP_MAX) {
        if (s_heater_state) {
            s_heater_state = false;
            heater_set(false);
            ESP_LOGI(TAG, "Heater OFF: T=%.1f°C >= %.0f°C threshold",
                     temperature_c, TEMP_MAX);
        }
    }
    /* 否则保持当前状态（22 < T < 28） */
}

/* ================================================================
 * 换水泵智能控制
 * ================================================================
 *   pH 或 salinity 任一超出范围 → 双泵开启
 *   全部恢复正常 → 双泵关闭
 */
void water_pump_control_smart(float ph, float salinity)
{
    bool ph_bad  = (ph < PH_MIN || ph > PH_MAX);
    bool sal_bad = (salinity < SALINITY_MIN || salinity > SALINITY_MAX);
    bool need_change = ph_bad || sal_bad;

    if (need_change && !s_water_pump_state) {
        s_water_pump_state = true;
        pump_control(1, actuator_on);
        pump_control(2, actuator_on);
        ESP_LOGI(TAG, "Water pumps ON: pH=%.2f(%s) Sal=%.1f(%s)",
                 ph,       ph_bad  ? "BAD" : "OK",
                 salinity, sal_bad ? "BAD" : "OK");
    } else if (!need_change && s_water_pump_state) {
        s_water_pump_state = false;
        pump_control(1, actuator_off);
        pump_control(2, actuator_off);
        ESP_LOGI(TAG, "Water pumps OFF: pH=%.2f Sal=%.1f — all in range",
                 ph, salinity);
    }
}

/* ================================================================
 * 状态查询函数
 * ================================================================ */
bool o2_pump_get_state(void)    { return s_o2_pump_state; }
bool heater_get_state(void)     { return s_heater_state; }
bool water_pump_get_state(void) { return s_water_pump_state; }

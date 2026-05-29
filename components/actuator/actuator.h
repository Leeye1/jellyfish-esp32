#ifndef ACTUATOR_H
#define ACTUATOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Actuator state enumeration
typedef enum {
    actuator_off = 0,
    actuator_on  = 1
} actuator_state_t;

/**
 * @brief Control actuator on/off by ID
 * @param id Actuator ID:
 *   1 = 排水泵  (GPIO39, N-MOSFET)
 *   2 = 补水泵  (GPIO40, N-MOSFET)
 *   3 = 氧气泵  (GPIO38, N-MOSFET)
 *   4 = 加热棒  (GPIO41, N-MOSFET)
 *   5 = 内循环泵 (GPIO42, N-MOSFET)
 * @param state Desired state (actuator_on or actuator_off)
 */
void actuator_control(uint8_t id, actuator_state_t state);

/* ── 底层开关（直接设置 GPIO）──────────────────── */

/** @brief 换水泵控制（actuator_control 的便捷封装） */
void pump_control(uint8_t pump_id, actuator_state_t state);

/** @brief 氧气泵开关 */
void o2_pump_set(bool on);

/** @brief 加热棒开关 */
void heater_set(bool on);

/** @brief 内循环泵开关（向后兼容，true=全速，false=停止） */
void circ_pump_set(bool on);

/** @brief 内循环泵 PWM 初始化（LEDC timer0，1kHz，10-bit） */
void circ_pump_pwm_init(void);

/** @brief 内循环泵调速
 *  @param percent 速度百分比 0–100（超出范围自动钳制） */
void circ_pump_set_speed(uint8_t percent);

/* ── 智能控制接口（含滞后逻辑）──────────────────── */

/**
 * @brief 氧气泵智能控制（滞后控制）
 * @param do_saturation 当前溶解氧饱和度（0.0-1.0）
 *
 * 控制逻辑：
 *   DO <= 0.75 → 开启泵（增加供氧）
 *   DO >= 0.99 → 关闭泵（节省能耗）
 *   0.75 < DO < 0.99 → 保持当前状态
 */
void o2_pump_control_smart(float do_saturation);

/**
 * @brief 加热棒智能控制（滞后控制）
 * @param temperature_c 当前水温（°C）
 *
 * 控制逻辑：
 *   T <= 22°C → 开启加热棒
 *   T >= 28°C → 关闭加热棒（绝不在高温时开启）
 *   22°C < T < 28°C → 保持当前状态
 */
void heater_control_smart(float temperature_c);

/**
 * @brief 换水泵智能控制
 * @param ph         当前 pH 值
 * @param salinity   当前盐度（ppt）
 *
 * 控制逻辑：
 *   pH < 8.0 或 pH > 8.4 或 salinity < 32 或 salinity > 35 → 双泵开启换水
 *   全部恢复正常范围 → 双泵关闭
 */
void water_pump_control_smart(float ph, float salinity);

/* ── 状态查询（用于日志和调试）──────────────────── */

bool o2_pump_get_state(void);
bool heater_get_state(void);
bool water_pump_get_state(void);
bool circ_pump_get_state(void);

#ifdef __cplusplus
}
#endif

#endif // ACTUATOR_H

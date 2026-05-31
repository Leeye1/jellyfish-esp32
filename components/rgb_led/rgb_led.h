#ifndef RGB_LED_H
#define RGB_LED_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RGB_LED_COUNT 30  // WS2812 LED strip has 30 pixels

// 初始化RGB灯带（30个LED）
void rgb_led_init(void);

// 对整条LED设置同一颜色
void rgb_led_set_color(uint8_t red, uint8_t green, uint8_t blue);

// 关闭所有LED
void rgb_led_clear(void);

// 刷新LED显示
void rgb_led_refresh(void);

/**
 * @brief 启动平滑渐变到目标颜色
 * @param target_r, target_g, target_b: 目标RGB值 (0-255)
 * @param duration_ms: 渐变持续时间（毫秒）
 * 
 * 内部状态机会在后续的 rgb_led_update() 调用中进行线性插值
 */
void rgb_led_set_color_smooth(uint8_t target_r, uint8_t target_g, uint8_t target_b, uint32_t duration_ms);

/**
 * @brief 启动五彩斑斓渐变效果
 * 
 * 颜色序列：红 → 黄 → 绿 → 青 → 蓝 → 紫 → 红
 * 完整循环时间为 60 秒（每种颜色渐变 10 秒），无限循环
 * 调用一次即可自动循环，需要在主程序中持续调用 rgb_led_update() 
 */
void rgb_led_colorful_gradient(void);

/**
 * @brief 更新LED动画状态（应在每个刷新周期调用，如100ms）
 * 
 * 根据当前时间进行线性插值计算，并刷新LED显示
 */
void rgb_led_update(void);

#ifdef __cplusplus
}
#endif

#endif // RGB_LED_H

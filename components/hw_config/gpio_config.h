#ifndef GPIO_CONFIG_H
#define GPIO_CONFIG_H

#include <stdint.h>
#include "driver/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

// GPIO pin assignment based on README block diagram / hardware schematics
// 请根据实际连接调整这些 GPIO

// RS485 transceiver control pins
#define GPIO_RS485_DE_RE   16  // DE/RE direction control
#define GPIO_RS485_TX      17  // UART1 TX
#define GPIO_RS485_RX      18  // UART1 RX

// Actuator MOSFET control pins (right side, GPIO38~41)
#define GPIO_O2_PUMP       38  // 氧气泵 (N-MOSFET control)
#define GPIO_PUMP_1        39  // 换水泵1 (N-MOSFET control)
#define GPIO_PUMP_2        40  // 换水泵2 (N-MOSFET control)
#define GPIO_HEATER        41  // 温度加热棒 (N-MOSFET control)
#define GPIO_CIRC_PUMP     42  // 内循环泵 (N-MOSFET control)

// RGB LED (WS2812 data pin)
#define GPIO_RGB_DATA      47  // 单线控制 WS2812

// Manual water pump toggle button (push button input)
#define GPIO_WATER_CHANGE_BTN  21  // 手动换水按钮 (Input with pull-up)

// Analog sensor input pins (ADC)
// 注意：GPIO36, GPIO39, GPIO34, GPIO35 因 PSRAM (GPIO33~GPIO37) 限制已禁用
// 重新分配至可用的 ADC 通道，避免冲突
#define ADC_PH_CHANNEL     ADC1_CHANNEL_0 // GPIO1 (replaced from GPIO36)

// 初始化所有 GPIO（输入/输出、PWM/ADC）
void gpio_config_init(void);

#ifdef __cplusplus
}
#endif

#endif // GPIO_CONFIG_H

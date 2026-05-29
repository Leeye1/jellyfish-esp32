#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the buzzer module
 * 
 * Configures GPIO pin for buzzer control (GPIO_BUZZER = 10)
 * By default, buzzer is turned off
 */
void buzzer_init(void);

/**
 * @brief Control buzzer on/off
 * 
 * @param enabled true to turn on buzzer, false to turn off
 */
void buzzer_set(bool enabled);

/**
 * @brief Play a beep pattern (useful for test/alarm)
 * 
 * @param count Number of beeps
 * @param duration_ms Duration of each beep in milliseconds
 * @param interval_ms Interval between beeps in milliseconds
 */
void buzzer_beep(uint8_t count, uint16_t duration_ms, uint16_t interval_ms);

#ifdef __cplusplus
}
#endif

#endif // BUZZER_H

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

#ifdef __cplusplus
}
#endif

#endif // BUZZER_H

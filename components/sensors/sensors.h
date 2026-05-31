#ifndef SENSORS_H
#define SENSORS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── 热带水母安全阈值 ──────────────────────────
#define TEMP_MIN        22.0f   // °C
#define TEMP_MAX        28.0f
#define PH_MIN          8.0f
#define PH_MAX          8.4f
#define SALINITY_MIN    32.0f   // ppt
#define SALINITY_MAX    35.0f
#define DO_SAT_MIN      0.75f   // 75 %  (0.0~1.0)
#define DO_SAT_MAX      0.99f   // 99 %

typedef struct {
    float temperature_c;    // 温度 (°C)
    float ph;               // pH值
    float dissolved_oxygen; // 溶解氧浓度 (mg/L)
    float do_saturation;    // 溶解氧饱和度 (0.0~1.0，1.0 = 100%)
    float salinity;         // 盐度 (ppt)
} sensor_data_t;

// 初始化传感器子系统（ADC、外设）
void sensors_init(void);

// 读取所有传感器并返回结构体
sensor_data_t sensors_read_all(void);

// 读取单项
float sensors_read_ph(void);

// 阈值判断 — 返回 true 表示超出安全范围
bool sensors_is_ph_out_of_range(float ph);
bool sensors_is_temperature_out_of_range(float temp);
bool sensors_is_do_out_of_range(float do_saturation);
bool sensors_is_salinity_out_of_range(float salinity);

// 综合判断：盐度/pH/DO 任一超范围（换水触发条件）
bool sensors_needs_water_change(const sensor_data_t *data);

#ifdef __cplusplus
}
#endif

#endif // SENSORS_H

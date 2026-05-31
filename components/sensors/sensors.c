#include "sensors.h"
#include "gpio_config.h"
#include "rs485.h"
#include "driver/adc.h"

#include "esp_log.h"
#include "esp_random.h"

static const char* TAG = "sensors";

// 传感器从设备地址配置
// DO传感器地址 0x02, 盐度传感器地址 0x01
#define RS485_DO_SLAVE_ID       0x02
#define RS485_SALINITY_SLAVE_ID 0x01

// ADC 满量程参考电压（ESP32-S3, ADC_ATTEN_DB_12 ≈ 3.3V）
#define ADC_REF_VOLTAGE 3.3f
// pH 传感器经分压后最大输出电压（10kΩ + 15kΩ 分压 5V → 3.0V）
#define PH_MAX_VOLTAGE  3.0f
// pH 校准偏移量（在 pH7.00 标准液中测量后填入）
// 校准方法：将电极放入 pH7.00 溶液（或短接 BNC），读取 pH 值，
//          Offset = 7.00 - 读取值。例如读到 6.88，则 Offset = 0.12
#define PH_OFFSET       -1.10f

// ADC原始读数转换为实际电压（V）
// ADC1 12-bit: 0..4095 对应 0..ADC_REF_VOLTAGE
static float adc_to_voltage(adc1_channel_t channel)
{
    int raw = adc1_get_raw(channel);
    return raw * (ADC_REF_VOLTAGE / 4095.0f);
}

void sensors_init(void)
{
    // 初始化 RS485 子系统，用于DO和盐度传感器通信
    rs485_init();
    
    ESP_LOGI(TAG, "Sensors initialized");
}

float sensors_read_ph(void)
{
    // pH 传感器模拟输出经分压后接 ADC
    // 分压: 10kΩ + 15kΩ → 5V 变 3.0V
    // 线性映射: 0V=pH0, 3.0V=pH14, pH7≈1.5V（分压后）
    //
    // 校准流程（参照说明书）:
    //   1. 电极插入 pH7.00 标准液（或短接 BNC），记录读数
    //   2. 将差值写入 PH_OFFSET（例: 读到 6.88 → Offset=0.12）
    //   3. 电极插入 pH4.00 标准液，调节板上增益电位器使读数≈4.00
    //   4.（可选）电极插入 pH9.18 标准液，微调电位器使读数≈9.18
    //   注意: 每换一种溶液前须用去离子水清洗电极
    
    int raw = adc1_get_raw(ADC_PH_CHANNEL);
    float v = adc_to_voltage(ADC_PH_CHANNEL);

    if (raw < 10) {
        ESP_LOGW(TAG, "pH ADC raw is near zero (raw=%d, ch=%d). Check pH probe power/signal wiring.",
                 raw, ADC_PH_CHANNEL);
    }
    
    // 线性映射 + 校准偏移
    float ph = (v / PH_MAX_VOLTAGE) * 14.0f + PH_OFFSET;
    
    // 确保在合理范围内（0-14）
    if (ph < 0.0f) ph = 0.0f;
    if (ph > 14.0f) ph = 14.0f;
    
    ESP_LOGD(TAG, "pH: raw=%d voltage=%.2f V, pH=%.2f (offset=%.2f)", raw, v, ph, PH_OFFSET);
    
    return ph;
}

// 读取所有传感器并返回结构体
sensor_data_t sensors_read_all(void)
{
    sensor_data_t d = {0};

#ifdef DEMO_MODE
    d.temperature_c = sensors_add_random_offset(25.0f);
    d.ph = sensors_add_random_offset(8.2f);
    d.dissolved_oxygen = sensors_add_random_offset(7.5f);
    d.do_saturation = sensors_add_random_offset_with_range(0.90f, 0.02f);
    d.salinity = sensors_add_random_offset(33.5f);
    ESP_LOGI(TAG, "Sensors (demo): T=%.2f°C  DO=%.2f mg/L(%.0f%%)  pH=%.2f  Sal=%.2f ppt",
             d.temperature_c, d.dissolved_oxygen, d.do_saturation * 100, d.ph, d.salinity);
#else
    float sal_temp = 0.0f;
    if (rs485_read_salinity(RS485_SALINITY_SLAVE_ID, &d.salinity, &sal_temp) == ESP_OK) {
        d.temperature_c = sal_temp;
    } else {
        ESP_LOGW(TAG, "Failed to read salinity sensor");
    }

    float do_sat = 0.0f;
    if (rs485_read_do(RS485_DO_SLAVE_ID, &do_sat, &d.dissolved_oxygen, NULL) == ESP_OK) {
        d.do_saturation = do_sat;
    } else {
        ESP_LOGW(TAG, "Failed to read DO sensor");
    }

    d.ph = sensors_read_ph();

    ESP_LOGI(TAG, "T=%.2f°C  DO=%.2f mg/L(%.0f%%)  pH=%.2f  Sal=%.2f ppt",
             d.temperature_c, d.dissolved_oxygen, d.do_saturation * 100, d.ph, d.salinity);
#endif
    return d;
}

float sensors_add_random_offset(float reference_value)
{
    return sensors_add_random_offset_with_range(reference_value, 0.2f);
}

float sensors_add_random_offset_with_range(float reference_value, float offset)
{
    float min_value = reference_value - offset;
    float max_value = reference_value + offset;
    uint32_t random_uint = esp_random();
    float random_normalized = (float)(random_uint % 10000) / 10000.0f;
    return min_value + random_normalized * (max_value - min_value);
}

bool sensors_is_ph_out_of_range(float ph)
{
    return (ph < PH_MIN || ph > PH_MAX);
}

bool sensors_is_temperature_out_of_range(float temp)
{
    return (temp < TEMP_MIN || temp > TEMP_MAX);
}

bool sensors_is_do_out_of_range(float do_saturation)
{
    return (do_saturation < DO_SAT_MIN || do_saturation > DO_SAT_MAX);
}

bool sensors_is_salinity_out_of_range(float salinity)
{
    return (salinity < SALINITY_MIN || salinity > SALINITY_MAX);
}

bool sensors_needs_water_change(const sensor_data_t *data)
{
    return sensors_is_ph_out_of_range(data->ph)
        || sensors_is_do_out_of_range(data->do_saturation)
        || sensors_is_salinity_out_of_range(data->salinity);
}



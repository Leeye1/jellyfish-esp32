#include "rs485.h"
#include "gpio_config.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"  // esp_rom_delay_us

// RS485子系统负责引导Modbus/RTU通信。
// - DE/RE由GPIO_RS485_DE_RE控制。
// - 波特率固定为9600 baud（可通过rs485_set_baudrate临时改变，用于配置传感器）
// - 数据收发通过UART1 TX/RX完成，与RS485收发器串联。
static const char* TAG = "rs485";
static int current_baudrate = 9600;
static bool rs485_initialized = false;

// 计算 Modbus CRC16（公开接口，供其他模块复用）
uint16_t rs485_modbus_crc16(const uint8_t* data, int len)
{
    uint16_t crc = 0xFFFF;
    for (int pos = 0; pos < len; pos++) {
        crc ^= data[pos];
        for (int i = 0; i < 8; i++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// 将4个字节解读为IEEE 754浮点数（大端模式）
// 使用 memcpy 而非指针强转，避免严格别名规则下的 UB
static float bytes_to_float_be(const uint8_t* bytes)
{
    uint32_t val = ((uint32_t)bytes[0] << 24) |
                   ((uint32_t)bytes[1] << 16) |
                   ((uint32_t)bytes[2] << 8)  |
                   ((uint32_t)bytes[3]);
    float f;
    memcpy(&f, &val, sizeof(f));
    return f;
}

int rs485_get_baudrate(void)
{
    return current_baudrate;
}

void rs485_init(void)
{
    if (rs485_initialized) {
        ESP_LOGW(TAG, "RS485 already initialized, skipping");
        return;
    }

    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, GPIO_RS485_TX, GPIO_RS485_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_1, 512, 512, 0, NULL, 0);

    rs485_set_transmit_mode(false);
    rs485_initialized = true;

    ESP_LOGI(TAG, "RS485 initialized at 9600 baud");
}

esp_err_t rs485_set_baudrate(int baudrate)
{
    if (current_baudrate == baudrate) {
        return ESP_OK;
    }

    uart_set_baudrate(UART_NUM_1, baudrate);
    current_baudrate = baudrate;
    uart_flush_input(UART_NUM_1);  // 清除波特率切换期间产生的噪声字节
    ESP_LOGI(TAG, "RS485 baudrate changed to %d", baudrate);
    return ESP_OK;
}

static void rs485_set_transmit_mode(bool enable)
{
    gpio_set_level(GPIO_RS485_DE_RE, enable ? 1 : 0);
}

esp_err_t rs485_send_bytes(const uint8_t* data, size_t len)
{
    // 调试日志：打印发送的原始 hex
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_INFO);

    rs485_set_transmit_mode(true);
    int sent = uart_write_bytes(UART_NUM_1, (const char*)data, len);
    uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(100));
    // uart_wait_tx_done 仅等待 TX FIFO 清空，移位寄存器最多还有 1 字节在发。
    // 必须用 esp_rom_delay_us（不受 tick 粒度影响）。
    // 动态计算：1字节 = 10 bits / baudrate 秒，加安全余量
    int byte_us = 10 * 1000000 / current_baudrate;  // 单字节传输时间 µs
    esp_rom_delay_us(byte_us + 500);  // 等待最后一字节移出移位寄存器
    rs485_set_transmit_mode(false);
    // 等待可能的 TX 回显字节进入 RX FIFO 后清除
    // 使用 3 个字节时间作为安全余量（小于 Modbus 3.5 char 帧间隔）
    esp_rom_delay_us(byte_us * 3);
    uart_flush_input(UART_NUM_1);
    return (sent == (int)len) ? ESP_OK : ESP_FAIL;
}

int rs485_receive_bytes(uint8_t* buffer, size_t max_len)
{
    // 增加超时时间到 500ms，确保完整接收长响应
    int len = uart_read_bytes(UART_NUM_1, buffer, max_len, 500 / portTICK_PERIOD_MS);
    if (len > 0) {
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, buffer, len, ESP_LOG_INFO);
    }
    return len;
}

esp_err_t rs485_read_holding_registers(uint8_t slave_id, uint16_t start_addr, uint16_t num_regs, uint8_t* response, size_t response_size, size_t* out_len)
{
    // 发送前清空接收缓冲区，消除上次事务的残留数据
    uart_flush_input(UART_NUM_1);

    // 构造Modbus读保持寄存器命令 (功能码 0x03)
    uint8_t request[8];
    request[0] = slave_id;
    request[1] = 0x03; // 功能码：读保持寄存器
    request[2] = (start_addr >> 8) & 0xFF;
    request[3] = start_addr & 0xFF;
    request[4] = (num_regs >> 8) & 0xFF;
    request[5] = num_regs & 0xFF;
    uint16_t crc = rs485_modbus_crc16(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8) & 0xFF;

    esp_err_t err = rs485_send_bytes(request, sizeof(request));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send Modbus request");
        return err;
    }

#ifdef RS485_DIAG
    // ── 诊断：分段检查 RX 缓冲区是否有数据到达 ──
    for (int wait_step = 0; wait_step < 6; wait_step++) {
        vTaskDelay(pdMS_TO_TICKS(50));  // 每 50ms 检查一次
        size_t buffered = 0;
        uart_get_buffered_data_len(UART_NUM_1, &buffered);
        if (buffered > 0) {
            ESP_LOGI(TAG, "DIAG: %d bytes in RX buffer after %dms", (int)buffered, (wait_step + 1) * 50);
            break;
        }
        if (wait_step == 5) {
            ESP_LOGW(TAG, "DIAG: 0 bytes after 300ms — RX path may be disconnected! "
                     "Check: RS485 transceiver RO pin → GPIO%d", GPIO_RS485_RX);
        }
    }
#endif
    // 等待响应到达（Modbus 3.5 char 帧间隔 ≈ 3.6ms @9600baud，50ms 安全余量）
    vTaskDelay(pdMS_TO_TICKS(50));
    int len = rs485_receive_bytes(response, response_size);
    if (len <= 0) {
        ESP_LOGW(TAG, "No response received from slave 0x%02X", slave_id);
        return ESP_FAIL;
    }
    
    ESP_LOGD(TAG, "Received %d bytes from slave 0x%02X", len, slave_id);

    // 最小长度：从站(1)+功能(1)+字节数(1)+数据(2*n)+CRC(2)
    int min_len = 5 + 2 * num_regs;
    if (len < min_len) {
        ESP_LOGW(TAG, "Response too short: got %d bytes, expected at least %d", len, min_len);
        return ESP_FAIL;
    }

    // 搜索有效帧起始（容忍前置噪声字节）：查找 [slave_id][0x03] 模式
    int frame_start = -1;
    for (int i = 0; i <= len - min_len; i++) {
        if (response[i] == slave_id && response[i + 1] == 0x03) {
            frame_start = i;
            break;
        }
    }
    if (frame_start < 0) {
        ESP_LOGW(TAG, "No valid Modbus frame found in response from slave 0x%02X", slave_id);
        return ESP_FAIL;
    }
    if (frame_start > 0) {
        ESP_LOGD(TAG, "Skipping %d leading noise byte(s)", frame_start);
        memmove(response, response + frame_start, len - frame_start);
        len -= frame_start;
        if (len < min_len) {
            ESP_LOGW(TAG, "Response too short after frame alignment: %d bytes", len);
            return ESP_FAIL;
        }
    }

    // 检查字节数字段
    if (response[2] != 2 * num_regs) {
        ESP_LOGW(TAG, "Mismatch in byte count: got %d, expected %d", response[2], 2 * num_regs);
        return ESP_FAIL;
    }

    // 校验 CRC（低字节在前，高字节在后）
    // 使用精确帧长度（而非 len，后者可能含尾部噪声字节）
    int frame_len = 3 + response[2] + 2;  // [addr][func][byte_count][data...][crc_lo][crc_hi]
    if (len < frame_len) {
        ESP_LOGW(TAG, "Response too short for CRC: got %d, need %d", len, frame_len);
        return ESP_FAIL;
    }
    uint16_t resp_crc = response[frame_len - 2] | (response[frame_len - 1] << 8);
    if (rs485_modbus_crc16(response, frame_len - 2) != resp_crc) {
        ESP_LOGW(TAG, "CRC check failed");
        return ESP_FAIL;
    }

    if (out_len) *out_len = frame_len;
    return ESP_OK;
}

esp_err_t rs485_read_float(uint8_t slave_id, uint16_t start_addr, float* value)
{
    if (!value) return ESP_ERR_INVALID_ARG;

    uint8_t buffer[13];
    size_t read_len = 0;
    
    // 读取2个寄存器（4字节）
    esp_err_t err = rs485_read_holding_registers(slave_id, start_addr, 2, buffer, sizeof(buffer), &read_len);
    if (err != ESP_OK) {
        return err;
    }

    // 响应格式：[从站][功能码][字节数(4)][高字节高位][高字节低位][低字节高位][低字节低位][CRC低][CRC高]
    if (read_len < 9 || buffer[2] != 0x04) {
        ESP_LOGW(TAG, "Invalid response format for float read");
        return ESP_FAIL;
    }

    // 大端模式：ABCD（字节3,4,5,6为浮点数数据）
    *value = bytes_to_float_be(&buffer[3]);
    return ESP_OK;
}

esp_err_t rs485_read_uint16(uint8_t slave_id, uint16_t start_addr, uint16_t* value)
{
    if (!value) return ESP_ERR_INVALID_ARG;

    uint8_t buffer[9];
    size_t read_len = 0;
    
    // 读取1个寄存器（2字节）
    esp_err_t err = rs485_read_holding_registers(slave_id, start_addr, 1, buffer, sizeof(buffer), &read_len);
    if (err != ESP_OK) {
        return err;
    }

    // 响应格式：[从站][功能码][字节数(2)][数据高][数据低][CRC低][CRC高]
    if (read_len < 7 || buffer[2] != 0x02) {
        ESP_LOGW(TAG, "Invalid response format for uint16 read");
        return ESP_FAIL;
    }

    // 大端模式
    *value = ((uint16_t)buffer[3] << 8) | buffer[4];
    return ESP_OK;
}

esp_err_t rs485_read_do(uint8_t slave_id, float* do_saturation, float* do_mgL, float* temperature)
{
    // 溶解氧传感器 Modbus-RTU 读取
    // 寄存器0x0000~0x0005：6个寄存器，3个浮点数（大端模式）
    // 0x0000~0x0001：溶解氧饱和度
    // 0x0002~0x0003：溶解氧浓度（mg/L）
    // 0x0004~0x0005：温度（°C）

    uint8_t buffer[20];
    size_t read_len = 0;
    
    esp_err_t err = rs485_read_holding_registers(slave_id, 0x0000, 6, buffer, sizeof(buffer), &read_len);
    if (err != ESP_OK) {
        return err;
    }

    // 响应格式：[从站][功能][字节数(12)][数据×12][CRC低][CRC高]
    if (read_len < 16 || buffer[2] != 0x0C) {
        ESP_LOGW(TAG, "Invalid response format for DO sensor");
        return ESP_FAIL;
    }

    // 解析3个浮点数数据（每个4字节，从buffer[3]开始）
    if (do_saturation) *do_saturation = bytes_to_float_be(&buffer[3]);   // 0x0000~0x0001
    if (do_mgL)        *do_mgL = bytes_to_float_be(&buffer[7]);          // 0x0002~0x0003
    if (temperature)   *temperature = bytes_to_float_be(&buffer[11]);     // 0x0004~0x0005

    return ESP_OK;
}

esp_err_t rs485_read_salinity(uint8_t slave_id, float* salinity_ppt, float* temperature)
{
    // 一次读取两个连续寄存器，避免两次独立 Modbus 事务
    // 0x0000: 盐度（uint16，实际值 = 寄存器值 / 100）
    // 0x0001: 温度（uint16，实际值 = 寄存器值 / 10）
    uint8_t buffer[11];
    size_t read_len = 0;

    esp_err_t err = rs485_read_holding_registers(slave_id, 0x0000, 2, buffer, sizeof(buffer), &read_len);
    if (err != ESP_OK) {
        return err;
    }

    // 响应：[slave][0x03][0x04][sal_hi][sal_lo][temp_hi][temp_lo][crc_lo][crc_hi]
    // buffer[2] = 0x04（2个寄存器=4字节），已在 rs485_read_holding_registers 内验证
    uint16_t sal_raw  = ((uint16_t)buffer[3] << 8) | buffer[4];
    uint16_t temp_raw = ((uint16_t)buffer[5] << 8) | buffer[6];

    if (salinity_ppt) *salinity_ppt = sal_raw  / 100.0f;
    if (temperature)  *temperature  = temp_raw / 10.0f;

    return ESP_OK;
}


#ifndef RS485_H
#define RS485_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化 RS485 串口（固定波特率9600，重复调用安全）
void rs485_init(void);

// 获取当前波特率
int rs485_get_baudrate(void);

// 设置波特率（用于临时切换，如修改传感器配置）
esp_err_t rs485_set_baudrate(int baudrate);

// Modbus CRC16 计算（公开接口）
uint16_t rs485_modbus_crc16(const uint8_t* data, int len);

// 清空接收缓冲区（丢弃所有待读数据）
void rs485_flush_rx(void);

// 将模块置为发送或接收
void rs485_set_transmit_mode(bool enable);

// 发送 Modbus/RTU 字节流
esp_err_t rs485_send_bytes(const uint8_t* data, size_t len);

// 读取字节，非阻塞
int rs485_receive_bytes(uint8_t* buffer, size_t max_len);

// 读取指定 Modbus 寄存器（支持16位整数和32位浮点数）
esp_err_t rs485_read_holding_registers(uint8_t slave_id, uint16_t start_addr, uint16_t num_regs, uint8_t* response, size_t response_size, size_t* out_len);

// 读取浮点数类型的寄存器（两个16位寄存器 = 32位浮点）
esp_err_t rs485_read_float(uint8_t slave_id, uint16_t start_addr, float* value);

// 读取16位无符号整数寄存器
esp_err_t rs485_read_uint16(uint8_t slave_id, uint16_t start_addr, uint16_t* value);

// 读取 DO 传感器（溶解氧，地址0x02，寄存器0x0000~0x0005）
esp_err_t rs485_read_do(uint8_t slave_id, float* do_saturation, float* do_mgL, float* temperature);

// 读取盐度传感器（地址0x01，寄存器0x0000~0x0001）
esp_err_t rs485_read_salinity(uint8_t slave_id, float* salinity_ppt, float* temperature);

#ifdef __cplusplus
}
#endif

#endif // RS485_H

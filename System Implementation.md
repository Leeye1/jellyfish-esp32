# Jellyfish 水族箱控制系统模块说明（稳定运行版）

本文档描述当前项目的模块设计、数据路径和任务协作关系。

## 1. 模块划分

- hw_config
  - 文件: components/hw_config/gpio_config.h, components/hw_config/gpio_config.c
  - 功能: GPIO/ADC 初始化，执行器与 RS485 管脚统一配置

- sensors
  - 文件: components/sensors/sensors.h, components/sensors/sensors.c
  - 功能: 读取温度、pH、溶解氧、盐度，并提供阈值判断接口

- rs485
  - 文件: components/rs485/rs485.h, components/rs485/rs485.c
  - 功能: UART1 + Modbus RTU，读取 DO 和盐度传感器

- actuator
  - 文件: components/actuator/actuator.h, components/actuator/actuator.c
  - 功能: 控制排水泵、补水泵、氧气泵、加热棒、内循环泵

- buzzer
  - 文件: components/buzzer/buzzer.h, components/buzzer/buzzer.c
  - 功能: 蜂鸣器开关与短鸣

- rgb_led
  - 文件: components/rgb_led/rgb_led.h, components/rgb_led/rgb_led.c
  - 功能: WS2812 灯带控制（30 颗）

- network
  - 文件: components/network/network.h, components/network/network.c
  - 功能: WiFi + MQTT 连接与 JSON 上报

- main
  - 文件: main/main.c
  - 功能: 创建 4 个 FreeRTOS 任务并维护系统状态机

## 2. 任务与状态机

当前运行 4 个任务:

| 任务 | 优先级 | 栈 | 周期 | 职责 |
|------|--------|-----|------|------|
| task_sensor_read | 5 | 4096B | 5s | 读取全部传感器并发 EVT_SENSOR_READY |
| task_control_logic | 5 | 4096B | 事件驱动 | 阈值判断 + 控制泵/蜂鸣器/加热棒 |
| task_network_publish | 3 | 4096B | 15s | MQTT 上报 JSON |
| task_led_effect | 2 | 3072B | 100ms | 绿灯常亮 / 红灯闪烁 |

系统状态:
- SYS_NORMAL
- SYS_ALARM_TEMP
- SYS_ALARM_WATER
- SYS_ALARM_BOTH

状态规则:
- 温度异常: T < 22 或 T > 28
- 水质异常: pH / DO 饱和度 / 盐度任一超范围
- 水质异常触发双泵换水
- 温度异常不触发换水，只触发报警

## 3. 数据流

1. task_sensor_read 每 5 秒调用 sensors_read_all
2. 最新数据写入共享变量 s_sensor_data
3. 置位 EVT_SENSOR_READY 通知 task_control_logic
4. task_control_logic 决策后更新 s_state 并控制执行器
5. task_network_publish 每 15 秒发布 s_sensor_data
6. task_led_effect 每 100ms 读取 s_state 刷新灯效

## 4. 初始化顺序

app_main 启动流程:
1. gpio_config_init
2. sensors_init
3. rgb_led_init
4. buzzer_init
5. network_init
6. o2_pump_set(true)
7. xEventGroupCreate
8. xTaskCreate 4 个任务

## 5. CMake 依赖

main/CMakeLists.txt 依赖:
- driver
- led_strip
- network
- hw_config
- actuator
- buzzer
- rgb_led
- rs485
- sensors
- esp_wifi
- esp_event
- nvs_flash
- mqtt

说明: 项目已移除预测模块组件与其构建依赖。

## 6. 稳定性测试目标

- 持续运行时任务无崩溃
- 传感器数据周期稳定
- MQTT 连接异常时可自动恢复
- 报警状态切换与执行器动作一致
- LED 状态显示与系统状态一致

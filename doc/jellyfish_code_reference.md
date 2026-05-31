# Jellyfish — ESP32-S3 水母养殖监控系统 · 代码参考

## 一、系统总览

基于 ESP32-S3-N16R8 的热带水母水族箱自动化监控系统。通过 RS485 Modbus RTU 读取溶解氧/盐度/温度传感器，ADC 读取 pH 传感器，执行滞后控制驱动水泵/加热棒/氧气泵，并通过 MQTT 上报遥测数据。

**目标物种**：热带水母（安全范围：22-28°C, pH 8.0-8.4, 盐度 32-35ppt, DO饱和 ≥75%）

---

## 二、代码架构

```
jellyfish_test_all/
├── main/
│   └── main.c                    # 系统入口：任务创建、状态机
├── components/
│   ├── hw_config/                # GPIO 引脚定义 & 初始化
│   │   ├── gpio_config.h
│   │   └── gpio_config.c
│   ├── sensors/                  # 传感器数据采集
│   │   ├── sensors.h
│   │   ├── sensors.c
│   │   └── CMakeLists.txt        # DEMO_MODE 宏（demo分支）
│   ├── rs485/                    # RS485 Modbus RTU 协议栈
│   │   ├── rs485.h
│   │   └── rs485.c
│   ├── actuator/                 # 执行器 GPIO 控制 & 智能滞后
│   │   ├── actuator.h
│   │   └── actuator.c
│   ├── buzzer/                   # 蜂鸣器 GPIO on/off
│   │   ├── buzzer.h
│   │   └── buzzer.c
│   ├── rgb_led/                  # WS2812 30灯带（60秒彩虹渐变）
│   │   ├── rgb_led.h
│   │   └── rgb_led.c
│   └── network/                  # WiFi STA + MQTT 客户端
│       ├── network.h
│       └── network.c
└── doc/
    └── jellyfish_code_reference.md
```

### 组件依赖关系

```
main.c
  ├── hw_config  ────────────────────────── (GPIO 初始化)
  ├── sensors ────────┬── rs485 ── hw_config
  │   (ADC+RS485)     │
  │                   └── hw_config
  ├── actuator ─────── hw_config, sensors
  ├── buzzer ────────────────────────────── (独立)
  ├── rgb_led ──────── hw_config            (独立)
  └── network ───────────────────────────── (独立)
```

### 数据流

```
[传感器硬件]                  [WiFi / MQTT Broker]
     │                               ▲
     ▼                               │
task_sensor_read (5s) ──► task_network_publish (15s)
     │
     ▼ EventGroup(EVT_SENSOR_READY)
task_control_logic ──► [actuator, buzzer, RGB LED]
```

---

## 三、FreeRTOS 任务架构

| 任务名 | 优先级 | 栈大小 | 触发方式 | 职责 |
|--------|--------|--------|----------|------|
| `task_sensor_read` | 5 | 4096B | 周期 5s | 调用 `sensors_read_all()`，写入共享数据，置位 `EVT_SENSOR_READY` |
| `task_control_logic` | 5 | 4096B | 事件驱动 | 等待 `EVT_SENSOR_READY`，运行状态机 + 滞后控制，控制蜂鸣器 |
| `task_button_handler` | 4 | 4096B | 周期 50ms | GPIO21 按钮去抖动，切换 `s_manual_water_mode` |
| `task_network_publish` | 3 | 4096B | 周期 15s | 读取共享传感器数据，MQTT 发布 JSON |
| `task_led_effect` | 2 | 3072B | 周期 100ms | 调用 `rgb_led_update()` 驱动 60秒彩虹渐变 |

**任务间通信**：`EventGroupBits`（传感器 → 控制逻辑）+ `portMUX_TYPE` 临界区保护共享变量。

---

## 四、组件详解

### 4.1 `hw_config` — GPIO 引脚定义

**文件**：`components/hw_config/gpio_config.h`, `gpio_config.c`

| 宏 | GPIO | 方向 | 用途 |
|----|------|------|------|
| `GPIO_RS485_DE_RE` | 16 | OUT | RS485 收发控制 |
| `GPIO_RS485_TX` | 17 | OUT | UART1 TX |
| `GPIO_RS485_RX` | 18 | IN | UART1 RX |
| `GPIO_O2_PUMP` | 38 | OUT | 氧气泵 N-MOSFET |
| `GPIO_PUMP_1` | 39 | OUT | 换水泵1（排水） |
| `GPIO_PUMP_2` | 40 | OUT | 换水泵2（补水） |
| `GPIO_HEATER` | 41 | OUT | 加热棒 N-MOSFET |
| `GPIO_CIRC_PUMP` | 42 | OUT | 内循环泵 PWM |
| `GPIO_RGB_DATA` | 47 | OUT | WS2812 数据线 |
| `GPIO_WATER_CHANGE_BTN` | 21 | IN | 手动换水按钮（上拉） |
| `ADC_PH_CHANNEL` | ADC1_CH0 (GPIO1) | ADC | pH 传感器模拟输入 |

**接口**：`void gpio_config_init(void)` — 初始化上述全部 GPIO。

---

### 4.2 `sensors` — 传感器数据采集

**文件**：`components/sensors/sensors.h`, `sensors.c`, `CMakeLists.txt`

**安全阈值**（定义在 `sensors.h`）：

| 参数 | 最小值 | 最大值 | 单位 |
|------|--------|--------|------|
| 温度 | 22.0 | 28.0 | °C |
| pH | 8.0 | 8.4 | — |
| 盐度 | 32.0 | 35.0 | ppt |
| DO 饱和度 | 0.75 (75%) | 0.99 (99%) | 0.0–1.0 |

**数据结构**：
```c
typedef struct {
    float temperature_c;
    float ph;
    float dissolved_oxygen;   // mg/L
    float do_saturation;       // 0.0~1.0
    float salinity;            // ppt
} sensor_data_t;
```

**核心流程** — `sensor_data_t sensors_read_all(void)`：

1. **RS485 盐度传感器**（地址 0x01）：一次 Modbus 事务读取寄存器 0x0000（盐度 uint16 ÷ 100）和 0x0001（温度 uint16 ÷ 10）
2. **RS485 DO 传感器**（地址 0x02）：一次 Modbus 事务读取寄存器 0x0000–0x0005（饱和度 f32 + 浓度 f32 + 温度 f32，大端 IEEE 754）
3. **ADC pH 传感器**：`adc1_get_raw(ADC_PH_CHANNEL)` → `adc_to_voltage()` → 线性映射 0–3.0V → pH 0–14 + 校准偏移

**公共接口**：

| 函数 | 说明 |
|------|------|
| `void sensors_init(void)` | 初始化 RS485 子系统 |
| `sensor_data_t sensors_read_all(void)` | 读取全部传感器 |
| `float sensors_read_ph(void)` | 单独读取 pH（ADC） |
| `bool sensors_is_ph_out_of_range(float)` | pH 超范围判断 |
| `bool sensors_is_temperature_out_of_range(float)` | 温度超范围判断 |
| `bool sensors_is_do_out_of_range(float)` | DO 饱和度超范围 |
| `bool sensors_is_salinity_out_of_range(float)` | 盐度超范围 |
| `bool sensors_needs_water_change(const sensor_data_t*)` | 综合：pH/DO/盐度任一异常 |

**Demo 模式**（仅 demo 分支）：

`CMakeLists.txt` 中定义 `DEMO_MODE` 后，`sensors_read_all()` 返回基于参考值的随机偏移数据（温度 25.0±0.2°C, pH 8.2±0.2, DO 7.5±0.2 mg/L, 盐度 33.5±0.2 ppt），无需真实传感器硬件。

---

### 4.3 `rs485` — RS485 Modbus RTU 协议栈

**文件**：`components/rs485/rs485.h`, `rs485.c`

**物理层**：UART1, 9600 baud, 8N1, GPIO16 (DE/RE 方向控制), GPIO17 (TX), GPIO18 (RX)

**协议层**：

| 函数 | 说明 |
|------|------|
| `rs485_init()` | 初始化 UART1、配置引脚 |
| `rs485_send_bytes(data, len)` | DE=高 → 发送 → 等待移位完成 → DE=低 → flush RX |
| `rs485_receive_bytes(buf, max)` | 阻塞读取（500ms 超时） |
| `rs485_read_holding_registers(id, addr, n, ...)` | 构造 Modbus 0x03 命令 → 发送 → 接收 → CRC 校验 → 帧对齐 |
| `rs485_modbus_crc16(data, len)` | CRC16 计算（多项式 0xA001） |
| `rs485_read_do(id, sat, mgl, temp)` | DO 传感器专用：读 6 个寄存器（3 个 float32） |
| `rs485_read_salinity(id, ppt, temp)` | 盐度传感器专用：读 2 个寄存器（1 个 uint16 ÷ 100, 1 个 uint16 ÷ 10） |

**保留工具函数**（目前无调用者，供未来扩展）：
- `rs485_read_float()` — 通用 float32 寄存器读取
- `rs485_read_uint16()` — 通用 uint16 寄存器读取
- `rs485_get_baudrate()` / `rs485_set_baudrate()` — 波特率查询/切换
- `#ifdef RS485_DIAG` 诊断块 — 分段轮询 RX 缓冲区，检查 RS485 物理链路

**CRC 校验 + 帧对齐**：`rs485_read_holding_registers()` 实现容忍前置噪声字节的帧搜索（查找 [slave_id][0x03] 模式），支持 memmove 重新对齐。

---

### 4.4 `actuator` — 执行器控制

**文件**：`components/actuator/actuator.h`, `actuator.c`

**底层 GPIO 控制**：

| 函数 | 说明 |
|------|------|
| `actuator_control(id, state)` | 按 ID 控制 GPIO（1=泵1, 2=泵2, 3=O2泵, 4=加热棒, 5=循环泵 PWM） |
| `pump_control(id, state)` | `actuator_control` 的薄封装 |
| `o2_pump_set(on)` | 氧气泵 GPIO38 |
| `heater_set(on)` | 加热棒 GPIO41 |
| `circ_pump_pwm_init()` | LEDC PWM 初始化（timer0, 1kHz, 10-bit, GPIO42） |
| `circ_pump_set_speed(percent)` | 循环泵 0–100% 调速 |

**滞后智能控制**（带死区，防止频繁开关）：

| 函数 | 控制逻辑 |
|------|----------|
| `o2_pump_control_smart(do_sat)` | DO ≤ 75% 开泵, DO ≥ 99% 关泵, 中间保持 |
| `heater_control_smart(temp)` | T ≤ 22°C 开加热, T ≥ 28°C 关加热, 中间保持 |
| `water_pump_control_smart(ph, sal)` | pH 或盐度任一超范围 → 双泵开；全部正常 → 双泵关 |

**手动模式守卫**（`main.c`）：
- 按钮按下 → `s_manual_water_mode = true` → `task_control_logic` 跳过 `water_pump_control_smart()`
- 再次按下 → 退出手动模式，恢复自动控制

**状态查询**：`o2_pump_get_state()`, `heater_get_state()`, `water_pump_get_state()`

---

### 4.5 `buzzer` — 蜂鸣器

**文件**：`components/buzzer/buzzer.h`, `buzzer.c`

| 函数 | 说明 |
|------|------|
| `buzzer_init()` | 配置 GPIO_BUZZER (GPIO10) 为输出，默认关闭 |
| `buzzer_set(enabled)` | GPIO10 on/off |

蜂鸣器由 `task_control_logic` 调用 `buzzer_set(temp_bad || water_bad)` 控制，任一传感器超范围即蜂鸣。

---

### 4.6 `rgb_led` — WS2812 LED 灯带

**文件**：`components/rgb_led/rgb_led.h`, `rgb_led.c`

**依赖**：`espressif/led_strip@^3.0.3`（RMT 驱动）

**硬件**：GPIO47, 30 像素 WS2812 (GRB 色彩顺序), 10MHz RMT 时钟

**五彩渐变效果**（`task_led_effect` 每 100ms 更新一帧）：

```
红 → 黄 → 绿 → 青 → 蓝 → 紫 → 红  （60秒完整循环）
```

实现方式：
- 6 种颜色定义在 `colorful_colors[]` 数组中
- `rgb_led_colorful_gradient()` 一次性初始化动画状态机
- `rgb_led_update()` 使用 `esp_timer_get_time()` 计算当前循环位置，在相邻颜色间做 RGB 线性插值
- 所有 30 个像素同步显示相同颜色

**彩色渐变循环计算**：
```c
cycle_position_ms = elapsed_ms % 60000;
current_segment   = cycle_position_ms / 10000;  // 每段 10s
progress          = (cycle_position_ms % 10000) / 10000.0f;  // 0.0–1.0
current_color     = lerp(colors[current], colors[next], progress);
```

**公共接口**：

| 函数 | 说明 |
|------|------|
| `rgb_led_init()` | 初始化 RMT + LED 灯带，清除所有像素 |
| `rgb_led_set_color(r, g, b)` | 设置整条灯带为固定颜色 |
| `rgb_led_clear()` | 关闭所有 LED |
| `rgb_led_refresh()` | 手动刷新（通常由 `rgb_led_update()` 自动处理） |
| `rgb_led_set_color_smooth(r, g, b, ms)` | 平滑过渡到目标颜色（通用工具，当前未被调用） |
| `rgb_led_colorful_gradient()` | 启动 60s 彩虹渐变循环 |
| `rgb_led_update()` | 每帧更新：处理彩虹渐变或平滑过渡插值 |

---

### 4.7 `network` — WiFi & MQTT

**文件**：`components/network/network.h`, `network.c`

**配置**：

| 参数 | 值 |
|------|-----|
| WiFi SSID | `"Tim"` |
| WiFi 密码 | `"12345678"` |
| MQTT Broker | `mqtt://172.20.10.6:1883` |
| 发布 Topic | `sensor/water` |
| QoS | 0 |
| 客户端 ID | `jellyfish_esp32` |

**接口**：

| 函数 | 说明 |
|------|------|
| `network_init()` | WiFi STA 连接 + MQTT 客户端启动 |
| `network_publish_sensor_data(const sensor_data_t*)` | 构造 JSON `{"temperature":..., "ph":..., "do":..., "salinity":...}` 并发布 |

**JSON 格式示例**：
```json
{"temperature":25.3,"ph":8.15,"dissolved_oxygen":7.2,"do_saturation":0.88,"salinity":33.7}
```

---

### 4.8 `main.c` — 系统入口 & 状态机

**文件**：`main/main.c`

**系统状态枚举**：

| 状态 | 触发条件 |
|------|----------|
| `SYS_NORMAL` | 所有参数正常 |
| `SYS_ALARM_TEMP` | 水温 < 22°C 或 > 28°C |
| `SYS_ALARM_WATER` | pH 或 DO 饱和度或盐度超范围 |
| `SYS_ALARM_BOTH` | 温度 + 水质同时异常 |

**启动流程**（`app_main`）：
```
gpio_config_init() → sensors_init() → rgb_led_init()
→ buzzer_init() → network_init()
→ o2_pump_set(true) + circ_pump_set_speed(35) + 其他执行器归零
→ 创建 5 个 FreeRTOS 任务
```

**事件驱动控制循环**（`task_control_logic`）：

1. 等待 `EVT_SENSOR_READY` 事件
2. 读取共享传感器数据
3. 状态机判断 → `set_system_state(next_state)`
4. 滞后智能控制：
   - `o2_pump_control_smart()` — 必执行
   - `heater_control_smart()` — 必执行
   - `water_pump_control_smart()` — **仅当 `!s_manual_water_mode`**（手动模式守卫）
5. `buzzer_set()` — 温度或水质异常时蜂鸣
6. 日志输出当前状态

---

## 五、分支差异

| | `master` | `demo` |
|--|----------|--------|
| **定位** | 生产版本 | 演示版本 |
| **传感器数据** | 真实 RS485 + ADC 读数 | `DEMO_MODE` 宏：mock 随机偏移数据 |
| **`sensors/CMakeLists.txt`** | 无 `DEMO_MODE` | `target_compile_definitions(... PRIVATE DEMO_MODE)` |
| **`sensors_add_random_offset()`** | 不存在 | 存在（`static`, 仅 sensors.c 内部使用） |
| **其他组件** | 完全相同 | 完全相同 |

---

## 六、构建 & 烧录

```powershell
# 前提：激活 ESP-IDF v5.5.3
C:\esp\v5.5.3\esp-idf\export.ps1

# 首次构建
idf.py set-target esp32s3
idf.py build

# 烧录 + 串口监控（COM12）
idf.py flash monitor
```

**依赖**：`espressif/led_strip@^3.0.3`（声明于 `main/idf_component.yml`）

**目标芯片**：ESP32-S3-N16R8 | **烧录端口**：COM12 | **波特率**：115200（默认）

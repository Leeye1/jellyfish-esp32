# Jellyfish 水母缸 — 主控逻辑说明（稳定运行版）

## 1. 系统架构概览

```
┌────────────────────────────────────────────────────────────────┐
│                     ESP32-S3 (FreeRTOS)                        │
│                                                                │
│  ┌──────────────┐   EVT_SENSOR_READY   ┌──────────────────┐   │
│  │ task_sensor   │ ─────────────────► │ task_control      │   │
│  │ (5s 周期)     │                     │ (阈值判断+执行器) │   │
│  │ 读取4路传感器 │                     └──────────────────┘   │
│  └──────────────┘                                             │
│                                                                │
│  ┌──────────────────┐      ┌──────────────┐                   │
│  │ task_network      │      │ task_led      │                   │
│  │ (15s 周期)        │      │ (100ms tick)  │                   │
│  │ MQTT发布JSON      │      │ 灯效循环      │                   │
│  └──────────────────┘      └──────────────┘                   │
└────────────────────────────────────────────────────────────────┘

外部连接:
  RS485 ─── DO传感器(9600baud) + 盐度传感器(9600baud)
  ADC   ─── pH传感器
  GPIO  ─── 排水泵(GPIO39) + 补水泵(GPIO40) + 氧气泵(GPIO38) + 加热棒(GPIO41) + 蜂鸣器(GPIO10)
  RMT   ─── WS2812 LED x30 (GPIO12)
  WiFi  ─── MQTT → 树莓派/云端
```

## 2. 任务详细说明

### 2.1 task_sensor_read（优先级5，栈4096B）

周期: 5 秒  
职责: 读取全部传感器并分发数据

```
每5秒执行:
  1. d = sensors_read_all()
     ├─ rs485_read_do(0x02) → DO饱和度/浓度/温度
     ├─ ADC 读取 GPIO1 → pH值
     └─ rs485_read_salinity(0x01) → 盐度
  2. s_sensor_data = d
  3. xEventGroupSetBits(EVT_SENSOR_READY)
```

### 2.2 task_control_logic（优先级5，栈4096B）

触发: EventGroup 等待 EVT_SENSOR_READY  
职责: 阈值判断 → 确定系统状态 → 控制执行器

```
等待新数据后:
  1. 阈值判断:
     ├─ temp_bad  = sensors_is_temperature_out_of_range(temp)
     └─ water_bad = sensors_needs_water_change(data)
                    → pH / DO_sat / 盐度 任一超范围

  2. 确定状态:
     ┌─────────────────────┬──────────────────────┐
     │ water_bad && temp_bad│ → SYS_ALARM_BOTH     │
     │ water_bad            │ → SYS_ALARM_WATER    │
     │ temp_bad             │ → SYS_ALARM_TEMP     │
     │ 其他                 │ → SYS_NORMAL          │
     └─────────────────────┴──────────────────────┘

  3. 执行器控制:
     ┌──────────────┬───────┬───────┬──────────┬──────┬──────┬────────────┐
     │ 状态          │ 泵1/2 │ 蜂鸣器│ LED      │ 氧气泵│ 加热棒│ 说明       │
     ├──────────────┼───────┼───────┼──────────┼──────┼──────┼────────────┤
     │ ALARM_WATER  │ ON/ON │ ON    │ 红色闪烁 │ ON   │ 视温度│ 换水+报警  │
     │ ALARM_BOTH   │ ON/ON │ ON    │ 红色闪烁 │ ON   │ 视温度│ 换水+报警  │
     │ ALARM_TEMP   │ OFF   │ ON    │ 红色闪烁 │ ON   │ 低温ON│ 仅报警     │
     │ NORMAL       │ OFF   │ OFF   │ 绿色常亮 │ ON   │ OFF  │ 正常运行   │
     └──────────────┴───────┴───────┴──────────┴──────┴──────┴────────────┘
```

换水逻辑要点:
- 触发条件: 盐度 / pH / DO 任一超出范围
- 双泵同时开启（排水量=补水量）
- 持续运行直到全部指标恢复正常
- 温度异常不触发换水（换水无法改变水温）

氧气泵控制:
- 氧气泵默认常开
- 通过 o2_pump_set() 控制（GPIO38）

加热棒控制:
- 仅在温度低于下限（22°C）时开启
- 温度回到正常后关闭
- 温度过高时绝不开启（只报警）
- 通过 heater_set() 控制（GPIO41）

### 2.3 task_network_publish（优先级3，栈4096B）

周期: 15 秒  
职责: MQTT 发布传感器数据 JSON

```
每15秒执行:
  network_publish_sensor_data(&s_sensor_data)
  → 序列化为 JSON:
    {"temperature":25.1,"ph":8.2,"oxygen":8.5,"salinity":33.5}
  → MQTT QoS1 发布到 topic: sensor/water
```

### 2.4 task_led_effect（优先级2，栈3072B）

周期: 100ms tick  
职责: 根据系统状态驱动 30 颗 WS2812 LED

```
每100ms:
  ┌────────────┬────────────────────┐
  │ SYS_NORMAL │ 30颗LED绿色常亮    │
  │ ALARM_*    │ 30颗LED红色闪烁    │
  └────────────┴────────────────────┘
```

## 3. 初始化序列

```
app_main()
  ├─ gpio_config_init()      GPIO / ADC 统一初始化
  ├─ sensors_init()          RS485 + ADC 传感器
  ├─ rgb_led_init()          WS2812 x30 (RMT)
  ├─ buzzer_init()           GPIO10
  ├─ network_init()          WiFi STA + MQTT
  ├─ o2_pump_set(true)       氧气泵常开
  ├─ xEventGroupCreate()     事件组
  └─ xTaskCreate() × 4       创建全部任务
```

## 4. 数据流向

```
                ┌──────────┐
   RS485/ADC ──►│ sensors  │
                └────┬─────┘
                     │ sensor_data_t
          ┌──────────┼──────────────────┐
          ▼          ▼                  ▼
   ┌────────────┐  ┌────────────┐  ┌──────────┐
   │ 控制逻辑   │  │ MQTT发布   │  │ 灯效任务 │
   │ (阈值判断) │  │ (JSON)     │  │ (状态显示)│
   └─────┬──────┘  └────────────┘  └──────────┘
         │
         ▼
   ┌───────────┐
   │ 泵/蜂鸣器 │
   │ 加热棒控制│
   └───────────┘
```

## 5. 阈值汇总（热带水母）

| 参数 | 安全范围 | 超范围动作 |
|------|---------|-----------|
| Temperature | 22–28 °C | 蜂鸣器报警 + 红灯（不换水） |
| pH | 8.0–8.4 | 双泵换水 + 蜂鸣器 + 红灯 |
| Salinity | 32–35 ppt | 双泵换水 + 蜂鸣器 + 红灯 |
| DO Saturation | 75–99 % | 双泵换水 + 蜂鸣器 + 红灯 |

## 6. 系统状态优先级

```
SYS_ALARM_BOTH  (最高)  水质+温度同时异常，双泵+报警
SYS_ALARM_WATER         水质异常，双泵换水+报警
SYS_ALARM_TEMP          仅温度异常，报警不换水
SYS_NORMAL     (最低)   全部正常，绿灯
```

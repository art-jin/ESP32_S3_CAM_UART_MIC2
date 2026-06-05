# Waveshare ESP32-S3-CAM-OV5640 硬件资源与兼容性分析

## 当前硬件方案（已实施）

| 功能 | 方案 | 接口 | GPIO |
|------|------|------|------|
| 双麦克风 | 板载 ES7210 4-ch ADC | I2S TDM | MCLK=10, BCLK=11, WS=12, DIN=13 |
| 扬声器 | 板载 ES8311 DAC | I2S STD | DOUT=14 |
| 摄像头 | 板载 OV5640 | DVP | XCLK=38, PCLK=41, VSYNC=17, HREF=18, D0-D7 |
| 舵机 | STS3215 串口总线舵机 | UART1 + Bus Servo Adapter A | TX=43, RX=44 (J6排针) |
| 控制台 | USB Serial JTAG | USB-C | — |
| I2C 总线 | 共享 | I2C0 | SCL=7, SDA=8 |

**所有功能可同时工作，无 GPIO 冲突。**

---

## 已解决的问题：舵机驱动

### 原问题：PCA9685 不可用

板载 ES7210（I2C 7-bit 地址 0x40）与 PCA9685（默认地址 0x40）在同一 I2C 总线上冲突。ES7210 初始化写 0xFF 到寄存器 0x00 会同时把 PCA9685 的 MODE1 设为睡眠模式，且 PCA9685 的物理存在会干扰 ES7210 的 I2C 读取。

### 解决方案：UART 串口总线舵机

- **舵机：** STS3215 串口总线舵机（1-DOF 水平转动，0°-180°，位置值 0-4095）
- **驱动板：** 微雪 Bus Servo Adapter A（半双工转换，TTL ↔ 单线）
- **连接：** ESP32-S3 J6 排针 GPIO43(TX) → Adapter UART RX, GPIO44(RX) ← Adapter UART TX
- **供电：** Adapter 外接 9-12.6V 电源，舵机 3-pin 信号线接 Adapter
- **软件：** `StsServo` 类（`main/boards/common/sts_servo.h/.cc`），UART_NUM_1, 1Mbps
- **关键前提：** 控制台从 UART0 切换到 USB Serial JTAG，释放 GPIO43/44

### 控制台切换

`sdkconfig.defaults.esp32s3` 中启用 `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`。日志通过 USB-C 线缆输出，GPIO43/44 空出给舵机 UART。

---

## GPIO 资源分配

| GPIO | 功能 | 备注 |
|------|------|------|
| 0 | BOOT 按钮 | 输入，短按唤醒/长按自定义 |
| 1-6 | LCD FPC (SPI) | J12 排线座，后续 LCD 使用 |
| 7 | I2C0 SCL | 共享总线：ES8311, ES7210, OV5640 SCCB |
| 8 | I2C0 SDA | 同上 |
| 9 | LCD FPC (SPI DC) | — |
| 10 | I2S MCLK | 音频主时钟 |
| 11 | I2S BCLK | 音频位时钟 |
| 12 | I2S WS | 音频字选择 |
| 13 | I2S DIN | ES7210 → ESP32（麦克风数据） |
| 14 | I2S DOUT | ESP32 → ES8311（扬声器数据） |
| 15 | LCD FPC (SPI CS) | — |
| 17 | DVP VSYNC | 摄像头 |
| 18 | DVP HREF | 摄像头 |
| 21 | DVP D7 | 摄像头数据线 |
| 38 | DVP XCLK | 摄像头主时钟（永久运行） |
| 39 | DVP D6 | 摄像头数据线 |
| 40 | DVP D5 | 摄像头数据线 |
| 41 | DVP PCLK | 摄像头像素时钟 |
| 42 | DVP D4 | 摄像头数据线 |
| 43 | UART1 TX | STS3215 舵机（J6 排针） |
| 44 | UART1 RX | STS3215 舵机（J6 排针） |
| 45 | DVP D0 | 摄像头数据线 |
| 46 | DVP D3 | 摄像头数据线 |
| 47 | DVP D1 | 摄像头数据线 |
| 48 | DVP D2 | 摄像头数据线 |

---

## ES7210 TDM 通道映射

ES7210 配置为 4 通道 TDM 模式（`ES7210_SEL_MIC1 | MIC2 | MIC3 | MIC4`）。实测确认的 TDM SLOT 与物理输入映射：

| TDM Slot | ES7210 ADC 输入 | 信号 | 用途 |
|----------|----------------|------|------|
| SLOT0 | ADC1 (MIC1P/MIC1N) | 物理 MEMS 麦克风 1 | AFE 主麦克风 + DOA |
| SLOT1 | ADC3 (ADC_MIC3_P/N) | AEC 回声消除参考 | ES8311 DAC 输出回路 → AFE 参考 |
| SLOT2 | ADC2 (MIC2P/MIC2N) | 物理 MEMS 麦克风 2 | DOA 第二麦克风 |
| SLOT3 | ADC4 (ADC_MIC4_P/N) | NC | 未连接 |

**识别方法：** SLOT1 仅在播放音频时有能量（AEC 参考信号），静默时接近零。SLOT0 和 SLOT2 在任何条件下都有相近的环境能量（物理麦克风）。

---

## 剩余限制

1. **J6 排针与 TF 卡共享：** GPIO43/44 与 TF 卡 CMD/D0 复用，使用舵机时不能使用 TF 卡
2. **I2C 总线拥挤：** 4 个设备共享一条 I2C 总线（ES8311, ES7210, OV5640 SCCB, CH32V003 IO Expander）
3. **无额外 GPIO：** 所有 GPIO 已分配，无法再增加外设（除非使用 LCD FPC 上的 GPIO，但会影响后续 LCD 功能）
4. **DOA 精度有限：** 双麦克风间距约 4cm，GCC-PHAT 在低频环境噪声下角度估计噪声较大

---

## 硬件参考

- 板卡文档：https://docs.waveshare.net/ESP32-S3-CAM-OVxxxx/?variant=ESP32-S3-CAM-OV5640
- 板卡原理图：https://www.waveshare.net/w/upload/9/9d/ESP32-S3-CAM-XXXX-schematic.pdf
- Bus Servo Adapter A：https://www.waveshare.net/wiki/Bus_Servo_Adapter_A
- STS3215 舵机：飞特串口总线舵机，通信协议兼容 StsServo 驱动

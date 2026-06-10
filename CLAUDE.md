# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-S3-CAM-OV5640 intelligent vision system based on **XiaoZhi V2.1.0** voice assistant framework. Features dual-microphone sound source localization (GCC-PHAT DOA), 1-DOF STS3215 serial bus servo pan control via UART, OV5640 camera, and 2inch ST7789 LCD with animated GIF emoji display. Custom board definition (`waveshare-s3-cam`) within the XiaoZhi codebase.

## Build Environment

**Always use ESP-IDF v5.5.2** (not v6.0.1, which also exists on this machine).

```bash
source ~/.espressif/v5.5.2/esp-idf/export.sh
idf.py build
idf.py -p <port> flash monitor
idf.py menuconfig
```

Target: `esp32s3`. Build system: ESP-IDF CMake + Ninja. Board: `CONFIG_BOARD_TYPE_WAVESHARE_S3_CAM=y`.
Serial port: `/dev/cu.usbmodem21201` (USB-C, USB Serial JTAG console). Log output is via USB Serial JTAG (not UART0).

## Architecture

Based on XiaoZhi V2.1.0 codebase (`reference/XiaoZhiCode_V2.1.0/` was copied to project root). The project adds:

1. **Board definition** `main/boards/waveshare-s3-cam/` — Waveshare ESP32-S3-CAM-OV5640 pin mappings and initialization
   - I2C bus: SDA=GPIO8, SCL=GPIO7 (shared by audio codecs ES8311/ES7210, camera SCCB, and IO expander CH32V003)
   - I2S: MCLK=GPIO10, BCLK=GPIO11, WS=GPIO12, DIN=GPIO13, DOUT=GPIO14
   - Audio: ES8311 (speaker DAC, I2C addr 0x00) + ES7210 (mic ADC, 4-ch TDM, I2C addr 0x40)
   - Camera: OV5640 via DVP (XCLK=GPIO38 permanent, PCLK=41, VSYNC=17, HREF=18, D0-D7)
   - Servo: STS3215 360° serial bus servo via UART1 (GPIO43 TX, GPIO44 RX) + Bus Servo Adapter A, geared to 92-tooth ring gear via 16-tooth driving gear (5.75:1 ratio), platform range ±30°
   - LCD: 2inch ST7789 via SPI2 (MOSI=GPIO1, CLK=GPIO5, CS=GPIO6, DC=GPIO3), reset+backlight via CH32V003 IO expander (I2C addr 0x24)
   - Console: USB Serial JTAG (frees UART0 GPIO43/44 for servo)

2. **Core XiaoZhi modules** (reused from base code):
   - Audio pipeline: `AudioService` → `BoxAudioCodec` → AFE → WakeNet ("你好小智") → Opus → WebSocket
   - MCP tools: `self.servo.shake_head`, `self.camera.take_photo`, `self.sound_tracking.start`, `self.sound_tracking.stop`
   - Protocols: WebSocket to `api.tenclass.net`, MQTT+UDP

3. **DOA sound source localization** (`main/boards/common/doa_tracker.h/.cc`)
   - GCC-PHAT algorithm using dual physical mic data from ES7210 4-channel TDM
   - MCP tools: `self.sound_tracking.start`, `self.sound_tracking.stop`
   - Auto-stop after 30s of no valid detection
   - DOA task runs on Core 0 at priority 4 (same as audio output; audio input at priority 8 preempts it)
   - Angle conversion injected via `std::function` (board-specific gear ratio), platform degrees centered at 0°

4. **LCD display with GIF emoji** (`main/boards/waveshare-s3-cam/waveshare_emoji_display.h/.cc`)
   - `WaveshareEmojiDisplay` extends `SpiLcdDisplay`, registers 6 animated GIF emojis via `EmojiCollection`
   - GIFs from `otto-emoji-gif-component` (240x240, ~216KB total): staticstate, happy, sad, anger, scare, buxue
   - GIF files embedded via `EMBED_FILES` in main CMakeLists.txt, accessed via `asm("_binary_<name>_gif_start")` extern declarations
   - Emoji occupies top half of 320x240 display; bottom half reserved for DOA overlay
   - 21 emotion names mapped to 6 GIFs (e.g., "happy"/"laughing"/"cool" → happy.gif, "thinking"/"confused" → buxue.gif)

5. **DOA LCD overlay** (in `waveshare_s3_cam_board.cc`)
   - Semi-transparent panel at bottom of screen with horizontal track bar and moving dot
   - Shows angle, SNR, noise floor, valid frame count
   - 5Hz update timer, auto-hides when tracking stopped

## ES7210 TDM Channel Mapping (Confirmed)

ES7210 outputs 4 TDM slots. The mapping between physical inputs and TDM slots:

| TDM Slot | ES7210 Input | Signal | Description |
|----------|-------------|--------|-------------|
| SLOT0 | ADC1 (MIC1P/MIC1N) | Physical MEMS mic 1 | Primary microphone |
| SLOT1 | ADC3 (ADC_MIC3_P/N) | AEC reference | ES8311 DAC output loopback for echo cancellation |
| SLOT2 | ADC2 (MIC2P/MIC2N) | Physical MEMS mic 2 | Secondary microphone |
| SLOT3 | ADC4 (ADC_MIC4_P/N) | NC | Not connected |

**Data flow in `audio_service.cc`:**
- AFE pipeline gets: SLOT0 (mic) + SLOT1 (AEC reference) → 2-channel "MR" format
- DOA tracker gets: SLOT0 (mic1) + SLOT2 (mic2) → 2-channel physical mic pair for direction estimation

**Important:** SLOT1 is NOT the second physical mic. It is the AEC reference signal (near-zero when no audio playback, high energy during playback). The second physical mic is on SLOT2.

## Hardware Wiring

| Peripheral | Interface | GPIOs | Details |
|---|---|---|---|
| OV5640 Camera | DVP (8-bit parallel) | XCLK=38, PCLK=41, VSYNC=17, HREF=18, D0-D7: 45,47,48,46,42,40,39,21 | Built into board |
| Dual MEMS Microphones | I2S TDM 4-ch via ES7210 | MCLK=10, BCLK=11, WS=12, DIN=13 | Built into board |
| Speaker (8Ω 2W) | I2S via ES8311 DAC | DOUT=14 | Board audio connector |
| STS3215 Servo | UART1 + Bus Servo Adapter A | TX=GPIO43, RX=GPIO44 (J6 header) | 1-DOF horizontal pan, 0°-180° range |
| 2inch LCD (ST7789) | SPI2 | MOSI=1, CLK=5, CS=6, DC=3 | 320x240 landscape, 40MHz SPI |
| LCD Reset/Backlight | CH32V003 IO expander (I2C 0x24) | RST=pin2, BL=pin1 (PWM reg 0x05) | Via I2C, not direct GPIO |
| Console/Logging | USB Serial JTAG | USB-C port | Replaces UART0 (freed GPIO43/44 for servo) |
| I2C Bus | Shared I2C0 | SCL=GPIO7, SDA=GPIO8 | ES8311 (0x00), ES7210 (0x40), OV5640 SCCB (0x3C), CH32V003 (0x24) |

**Note on I2C address 0x40:** ES7210 uses 7-bit address 0x40 (ES7210_CODEC_DEFAULT_ADDR=0x80, shifted right by 1). This was previously a conflict with PCA9685 (also 0x40), but PCA9685 is no longer used — servo now uses UART.

## Voice Commands (via XiaoZhi AI conversation → MCP tool calls)

- "请看我" → `self.sound_tracking.start` → DOA + servo + camera capture → AI vision analysis
- "停止看我" → `self.sound_tracking.stop` → stop DOA, servo to center
- "摇摇头" → `self.servo.shake_head` → platform sweep ±25° + oscillation ±15° × 3

## DOA Tracker Tuning Parameters

| Parameter | Value | Description |
|---|---|---|
| FFT_SIZE | 512 | FFT window size |
| MIC_SPACING_M | 0.04 | Distance between dual MEMS mics (4cm) |
| SAMPLE_RATE | 24000.0 | Must match actual I2S sample rate (was 16000, critical fix) |
| SMOOTH_ALPHA | 0.8 | Servo angle smoothing factor |
| SNR_THRESHOLD | 2.5 | Signal must be 2.5x noise floor |
| MIN_CORRELATION | 0.20 | GCC-PHAT peak threshold (lowered for 4cm spacing) |
| UPDATE_INTERVAL | 5 | Servo update every N frames |
| SERVO_MIN_INTERVAL_MS | 200 | Minimum interval between servo moves |
| AUTO_STOP_FRAMES | 900 | Auto-stop after ~30s no valid detection |

## Servo Gear Mechanism

STS3215 360° servo drives a 16-tooth gear (driving) meshing with 92-tooth ring gear (platform):
- Gear ratio: 92/16 = 5.75 (servo turns 5.75° per 1° platform rotation)
- Servo position 0-4095 = 0-360° shaft = 0-62.6° platform
- Platform center (position 2047) = 0° platform angle
- Platform range: approximately -31.3° to +31.3°, safety-limited to ±30°
- All application-layer angles are "platform degrees" (center=0°)
- Conversion functions in `config.h`: `PlatformDegFromPos()`, `PosFromPlatformDeg()`, `ClampPlatformAngle()`

## Key Files

- `main/boards/waveshare-s3-cam/config.h` — Pin definitions, LCD params, servo gear ratio + platform angle conversion
- `main/boards/waveshare-s3-cam/waveshare_s3_cam_board.cc` — Board initialization, MCP tools, DOA overlay
- `main/boards/waveshare-s3-cam/waveshare_emoji_display.h/.cc` — GIF emoji display class
- `main/boards/common/sts_servo.h/.cc` — STS3215 serial bus servo driver (UART)
- `main/boards/common/doa_tracker.h/.cc` — GCC-PHAT DOA sound source localization
- `main/boards/common/esp32_camera.h/.cc` — Camera driver
- `main/audio/codecs/box_audio_codec.h/.cc` — ES8311 + ES7210 codec wrapper (4-ch TDM, duplex I2S)
- `main/audio/audio_service.cc` — Audio pipeline, TDM channel extraction, DOA data feed
- `main/audio/processors/afe_audio_processor.cc` — AFE pipeline
- `main/display/lvgl_display/emoji_collection.h/.cc` — EmojiCollection base class for emoji lookup
- `main/display/lvgl_display/gif/lvgl_gif.h/.cc` — GIF animation controller
- `main/mcp_server.h/.cc` — MCP tool registration
- `main/CMakeLists.txt` — Build config, GIF EMBED_FILES for waveshare board
- `partitions/v2/16m.csv` — Partition table (dual OTA + 8MB assets)
- `sdkconfig.defaults.esp32s3` — ESP32-S3 specific SDK config

## Reference Code

- `reference/github-ESP32-S3-CAM-OVxxxx/examples/ESP-IDF-v5.5.1/02_esp_sr/` — ESP-SR reference (AFE, RMNM format, task structure)
- `reference/github-ESP32-S3-CAM-OVxxxx/examples/ESP-IDF-v5.5.1/01_simple_video_server/` — Camera DVP pin reference
- `reference/16PWMduojiBus/` — PCA9685 datasheet and Arduino test code (servo pulse 150-600 @ 50Hz)
- `reference/XiaoZhiCode_V2.1.0/` — Original XiaoZhi source (before board adaptation)
- `main/boards/otto-robot/otto_emoji_display.cc` — Reference pattern for GIF emoji registration

## Documentation Links

- Board docs: https://docs.waveshare.net/ESP32-S3-CAM-OVxxxx/?variant=ESP32-S3-CAM-OV5640
- Board schematic: https://www.waveshare.net/w/upload/9/9d/ESP32-S3-CAM-XXXX-schematic.pdf

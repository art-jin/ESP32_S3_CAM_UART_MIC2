#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/spi_master.h>

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_INPUT_REFERENCE    true

// I2S pins — Waveshare ESP32-S3-CAM-OV5640
#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_10
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_11
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_12
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_13
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_14

// I2C bus — shared by audio codecs (ES8311, ES7210) and camera SCCB
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_8
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_7
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR
#define AUDIO_CODEC_ES7210_ADDR  ES7210_CODEC_DEFAULT_ADDR

#define BUILTIN_LED_GPIO        GPIO_NUM_NC
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC

// OV5640 Camera DVP pins (XCLK permanent on GPIO38 — servo uses UART, no conflict)
#define CAMERA_PIN_PWDN   GPIO_NUM_NC
#define CAMERA_PIN_RESET  GPIO_NUM_NC
#define CAMERA_PIN_XCLK   GPIO_NUM_38
#define CAMERA_PIN_SIOD   GPIO_NUM_8   // shared I2C bus
#define CAMERA_PIN_SIOC   GPIO_NUM_7   // shared I2C bus

#define CAMERA_PIN_D7     GPIO_NUM_21
#define CAMERA_PIN_D6     GPIO_NUM_39
#define CAMERA_PIN_D5     GPIO_NUM_40
#define CAMERA_PIN_D4     GPIO_NUM_42
#define CAMERA_PIN_D3     GPIO_NUM_46
#define CAMERA_PIN_D2     GPIO_NUM_48
#define CAMERA_PIN_D1     GPIO_NUM_47
#define CAMERA_PIN_D0     GPIO_NUM_45
#define CAMERA_PIN_VSYNC  GPIO_NUM_17
#define CAMERA_PIN_HREF   GPIO_NUM_18
#define CAMERA_PIN_PCLK   GPIO_NUM_41

#define XCLK_FREQ_HZ 20000000

// STS3215 serial bus servo via UART (J6 header: GPIO43/44) + Bus Servo Adapter A
#define SERVO_UART_NUM     UART_NUM_1
#define SERVO_UART_TX_PIN  GPIO_NUM_43
#define SERVO_UART_RX_PIN  GPIO_NUM_44
#define SERVO_BAUD_RATE    1000000
#define SERVO_ID           1
#define SERVO_CENTER_POS   2047
#define SERVO_MAX_POS      4095

// 2inch Capacitive Touch LCD via SPI (FPC connector J12)
#define DISPLAY_SPI_HOST      SPI2_HOST
#define DISPLAY_MOSI_PIN      GPIO_NUM_1
#define DISPLAY_CLK_PIN       GPIO_NUM_5
#define DISPLAY_CS_PIN        GPIO_NUM_6
#define DISPLAY_DC_PIN        GPIO_NUM_3
#define DISPLAY_RST_PIN       GPIO_NUM_NC   // via CH32V003 IO expander pin 2
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_NC   // via CH32V003 IO expander pin 1

// ST7789 2inch LCD: 320x240 landscape (rotated from native 240x320 portrait)
#define DISPLAY_WIDTH         320
#define DISPLAY_HEIGHT        240
#define DISPLAY_OFFSET_X      0
#define DISPLAY_OFFSET_Y      0
#define DISPLAY_SWAP_XY       true
#define DISPLAY_MIRROR_X      true
#define DISPLAY_MIRROR_Y      false
#define DISPLAY_INVERT_COLOR  true
#define DISPLAY_SPI_CLK_HZ    (40 * 1000 * 1000)  // 40 MHz

#endif // _BOARD_CONFIG_H_

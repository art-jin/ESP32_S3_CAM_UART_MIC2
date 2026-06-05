#include "wifi_board.h"
#include "codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "sts_servo.h"
#include "esp32_camera.h"
#include "doa_tracker.h"
#include "mcp_server.h"
#include "waveshare_emoji_display.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>
#include <esp_rom_sys.h>
#include <esp_video_init.h>
#include <esp_cam_sensor.h>
#include <lvgl.h>
#include <esp_lvgl_port.h>

#define TAG "WaveshareS3CamBoard"

class WaveshareS3CamAudioCodec : public BoxAudioCodec {
public:
    WaveshareS3CamAudioCodec(i2c_master_bus_handle_t i2c_bus)
        : BoxAudioCodec(i2c_bus,
                        AUDIO_INPUT_SAMPLE_RATE,
                        AUDIO_OUTPUT_SAMPLE_RATE,
                        AUDIO_I2S_GPIO_MCLK,
                        AUDIO_I2S_GPIO_BCLK,
                        AUDIO_I2S_GPIO_WS,
                        AUDIO_I2S_GPIO_DOUT,
                        AUDIO_I2S_GPIO_DIN,
                        GPIO_NUM_NC,
                        AUDIO_CODEC_ES8311_ADDR,
                        AUDIO_CODEC_ES7210_ADDR,
                        AUDIO_INPUT_REFERENCE) {}
};

class WaveshareS3CamBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t io_expander_dev_ = nullptr;
    Button boot_button_;
    WaveshareEmojiDisplay* display_ = nullptr;
    StsServo* servo_ = nullptr;
    Esp32Camera* camera_ = nullptr;
    DoaTracker* doa_tracker_ = nullptr;

    // DOA LCD overlay objects
    lv_obj_t* doa_panel_ = nullptr;
    lv_obj_t* doa_track_ = nullptr;
    lv_obj_t* doa_dot_ = nullptr;
    lv_obj_t* doa_label_ = nullptr;
    lv_obj_t* doa_energy_label_ = nullptr;
    esp_timer_handle_t doa_timer_ = nullptr;

    static void DoaTimerCallback(void* arg) {
        auto* board = static_cast<WaveshareS3CamBoard*>(arg);
        board->UpdateDoaDisplay();
    }

    void CreateDoaOverlay() {
        if (!display_) return;

        DisplayLockGuard lock(display_);

        // Semi-transparent panel at the bottom of the screen
        doa_panel_ = lv_obj_create(lv_screen_active());
        lv_obj_set_size(doa_panel_, DISPLAY_WIDTH - 20, 50);
        lv_obj_align(doa_panel_, LV_ALIGN_BOTTOM_MID, 0, -5);
        lv_obj_set_style_bg_color(doa_panel_, lv_color_hex(0x1a1a2e), 0);
        lv_obj_set_style_bg_opa(doa_panel_, LV_OPA_90, 0);
        lv_obj_set_style_border_color(doa_panel_, lv_color_hex(0x4a90d9), 0);
        lv_obj_set_style_border_width(doa_panel_, 1, 0);
        lv_obj_set_style_radius(doa_panel_, 8, 0);
        lv_obj_set_style_pad_all(doa_panel_, 4, 0);
        lv_obj_add_flag(doa_panel_, LV_OBJ_FLAG_HIDDEN);

        // Horizontal track bar
        doa_track_ = lv_obj_create(doa_panel_);
        lv_obj_set_size(doa_track_, DISPLAY_WIDTH - 50, 6);
        lv_obj_align(doa_track_, LV_ALIGN_TOP_MID, 0, 8);
        lv_obj_set_style_bg_color(doa_track_, lv_color_hex(0x444466), 0);
        lv_obj_set_style_border_width(doa_track_, 0, 0);
        lv_obj_set_style_radius(doa_track_, 3, 0);
        lv_obj_set_scrollbar_mode(doa_track_, LV_SCROLLBAR_MODE_OFF);

        // Center mark on track
        lv_obj_t* center_mark = lv_obj_create(doa_track_);
        lv_obj_set_size(center_mark, 2, 10);
        lv_obj_align(center_mark, LV_ALIGN_CENTER, 0, -2);
        lv_obj_set_style_bg_color(center_mark, lv_color_hex(0x888888), 0);
        lv_obj_set_style_border_width(center_mark, 0, 0);
        lv_obj_set_style_radius(center_mark, 1, 0);

        // Moving dot indicator
        doa_dot_ = lv_obj_create(doa_track_);
        lv_obj_set_size(doa_dot_, 12, 12);
        lv_obj_align(doa_dot_, LV_ALIGN_CENTER, 0, -3);
        lv_obj_set_style_bg_color(doa_dot_, lv_color_hex(0x00ff88), 0);
        lv_obj_set_style_border_width(doa_dot_, 0, 0);
        lv_obj_set_style_radius(doa_dot_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_scrollbar_mode(doa_dot_, LV_SCROLLBAR_MODE_OFF);

        // Angle label
        doa_label_ = lv_label_create(doa_panel_);
        lv_label_set_text(doa_label_, "DOA: --");
        lv_obj_set_style_text_color(doa_label_, lv_color_hex(0x00ff88), 0);
        lv_obj_set_style_text_font(doa_label_, &lv_font_montserrat_14, 0);
        lv_obj_align(doa_label_, LV_ALIGN_BOTTOM_LEFT, 4, 0);

        // Energy label
        doa_energy_label_ = lv_label_create(doa_panel_);
        lv_label_set_text(doa_energy_label_, "");
        lv_obj_set_style_text_color(doa_energy_label_, lv_color_hex(0xaaaaaa), 0);
        lv_obj_set_style_text_font(doa_energy_label_, &lv_font_montserrat_14, 0);
        lv_obj_align(doa_energy_label_, LV_ALIGN_BOTTOM_RIGHT, -4, 0);

        // Timer to update DOA display at 5Hz
        esp_timer_create_args_t timer_args = {
            .callback = DoaTimerCallback,
            .arg = this,
            .name = "doa_disp"
        };
        esp_timer_create(&timer_args, &doa_timer_);
        esp_timer_start_periodic(doa_timer_, 200000);  // 200ms = 5Hz
    }

    void UpdateDoaDisplay() {
        if (!doa_tracker_ || !doa_panel_) return;

        bool is_running = doa_tracker_->IsRunning();
        if (!is_running) {
            if (!(lv_obj_has_flag(doa_panel_, LV_OBJ_FLAG_HIDDEN))) {
                DisplayLockGuard lock(display_);
                lv_obj_add_flag(doa_panel_, LV_OBJ_FLAG_HIDDEN);
            }
            return;
        }

        auto info = doa_tracker_->GetDebugInfo();

        DisplayLockGuard lock(display_);
        lv_obj_clear_flag(doa_panel_, LV_OBJ_FLAG_HIDDEN);

        // Move dot: angle -90..+90 maps to track width
        int track_half_w = (DISPLAY_WIDTH - 50) / 2 - 6;
        int x = (int)(info.angle_offset / 90.0f * track_half_w);
        if (x < -track_half_w) x = -track_half_w;
        if (x > track_half_w) x = track_half_w;
        lv_obj_set_x(doa_dot_, x);

        // Color: green if valid, red if not
        uint32_t color = info.is_valid ? 0x00ff88 : 0xff4444;
        lv_obj_set_style_bg_color(doa_dot_, lv_color_hex(color), 0);
        lv_obj_set_style_text_color(doa_label_, lv_color_hex(color), 0);

        // Angle text
        lv_label_set_text_fmt(doa_label_, "DOA: %+.0f%c", info.angle_offset,
                              info.is_valid ? ' ' : '?');

        // Energy text
        lv_label_set_text_fmt(doa_energy_label_, "SNR:%.1f N:%.0f V:%d F:%d",
                              info.snr, info.noise_floor,
                              info.valid_count, info.frame_count);
    }

    void RecoverI2cBus() {
        gpio_num_t sda = AUDIO_CODEC_I2C_SDA_PIN;
        gpio_num_t scl = AUDIO_CODEC_I2C_SCL_PIN;

        vTaskDelay(pdMS_TO_TICKS(100));

        gpio_config_t sda_cfg = {
            .pin_bit_mask = (1ULL << sda),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config_t scl_out_cfg = {
            .pin_bit_mask = (1ULL << scl),
            .mode = GPIO_MODE_OUTPUT_OD,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        gpio_config(&sda_cfg);
        gpio_config(&scl_out_cfg);

        int sda_level = gpio_get_level(sda);
        int scl_level = gpio_get_level(scl);

        if (sda_level == 0 || scl_level == 0) {
            ESP_LOGW(TAG, "I2C bus stuck (SDA=%d, SCL=%d), recovering...", sda_level, scl_level);
            for (int i = 0; i < 32; i++) {
                gpio_set_level(scl, 0);
                esp_rom_delay_us(10);
                gpio_set_level(scl, 1);
                esp_rom_delay_us(10);
                if (gpio_get_level(sda) != 0 && gpio_get_level(scl) != 0) {
                    break;
                }
            }
            gpio_config_t sda_out_cfg = {
                .pin_bit_mask = (1ULL << sda),
                .mode = GPIO_MODE_OUTPUT_OD,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            gpio_config(&sda_out_cfg);
            gpio_set_level(sda, 0);
            esp_rom_delay_us(10);
            gpio_set_level(scl, 1);
            esp_rom_delay_us(10);
            gpio_set_level(sda, 1);
            esp_rom_delay_us(10);
        }

        gpio_reset_pin(sda);
        gpio_reset_pin(scl);
    }

    void InitializeI2c() {
        RecoverI2cBus();

        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
        ESP_LOGI(TAG, "I2C bus initialized (SDA=%d, SCL=%d)", AUDIO_CODEC_I2C_SDA_PIN, AUDIO_CODEC_I2C_SCL_PIN);
    }

    void InitializeIoExpander() {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = 0x24,
            .scl_speed_hz = 100000,
        };
        ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_, &dev_cfg, &io_expander_dev_));

        uint8_t dir[] = {0x02, 0x77};
        ESP_ERROR_CHECK(i2c_master_transmit(io_expander_dev_, dir, sizeof(dir), -1));

        uint8_t all_on[] = {0x03, 0x77};
        ESP_ERROR_CHECK(i2c_master_transmit(io_expander_dev_, all_on, sizeof(all_on), -1));

        uint8_t pdn_low[] = {0x03, 0x72};
        ESP_ERROR_CHECK(i2c_master_transmit(io_expander_dev_, pdn_low, sizeof(pdn_low), -1));
        vTaskDelay(pdMS_TO_TICKS(50));

        ESP_ERROR_CHECK(i2c_master_transmit(io_expander_dev_, all_on, sizeof(all_on), -1));
        vTaskDelay(pdMS_TO_TICKS(50));

        ESP_LOGI(TAG, "CH32V003 IO expander initialized (addr=0x24, codecs+PA enabled)");
    }

    void InitializeLcd() {
        // Pulse LCD_RST + touch reset (CH32V003 pins 1,2, bits in register 0x03) low then high
        uint8_t rst_low[] = {0x03, 0x71};  // bits 1,2 cleared
        ESP_ERROR_CHECK(i2c_master_transmit(io_expander_dev_, rst_low, sizeof(rst_low), -1));
        vTaskDelay(pdMS_TO_TICKS(50));
        uint8_t rst_high[] = {0x03, 0x77};  // all high
        ESP_ERROR_CHECK(i2c_master_transmit(io_expander_dev_, rst_high, sizeof(rst_high), -1));
        vTaskDelay(pdMS_TO_TICKS(50));

        // Turn on backlight via CH32V003 PWM register (register 0x05, value 0-255)
        uint8_t pwm_on[] = {0x05, 0xFF};  // 100% brightness
        ESP_ERROR_CHECK(i2c_master_transmit(io_expander_dev_, pwm_on, sizeof(pwm_on), -1));

        // Init SPI bus
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

        // Create panel IO
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = 0;
        io_config.pclk_hz = DISPLAY_SPI_CLK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(DISPLAY_SPI_HOST, &io_config, &panel_io));

        // Create ST7789 panel
        esp_lcd_panel_handle_t panel = nullptr;
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_disp_on_off(panel, true);

        display_ = new WaveshareEmojiDisplay(panel_io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        ESP_LOGI(TAG, "ST7789 LCD initialized (%dx%d, SPI%d @ %d MHz, GIF emojis)",
                 DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_SPI_HOST, DISPLAY_SPI_CLK_HZ / 1000000);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            ESP_LOGI(TAG, "BOOT button clicked");
            auto& app = Application::GetInstance();
            auto state = app.GetDeviceState();
            ESP_LOGI(TAG, "Current state: %d", state);
            if (state == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        boot_button_.OnLongPress([this]() {
            ESP_LOGI(TAG, "BOOT button long-pressed -> servo shake test");
            ShakeHead();
        });
    }

    void InitializeServo() {
        servo_ = new StsServo(SERVO_UART_NUM, SERVO_UART_TX_PIN,
                              SERVO_UART_RX_PIN, SERVO_BAUD_RATE);
        if (servo_->Ping(SERVO_ID)) {
            servo_->EnableTorque(SERVO_ID, true);
            ESP_LOGI(TAG, "STS3215 servo found (ID=%d, UART%d, TX=%d, RX=%d)",
                     SERVO_ID, SERVO_UART_NUM, SERVO_UART_TX_PIN, SERVO_UART_RX_PIN);
        } else {
            ESP_LOGW(TAG, "STS3215 servo not responding (ID=%d)", SERVO_ID);
        }
    }

    void InitializeCamera() {
        static esp_cam_ctlr_dvp_pin_config_t dvp_pin_config = {
            .data_width = CAM_CTLR_DATA_WIDTH_8,
            .data_io = {
                [0] = CAMERA_PIN_D0,
                [1] = CAMERA_PIN_D1,
                [2] = CAMERA_PIN_D2,
                [3] = CAMERA_PIN_D3,
                [4] = CAMERA_PIN_D4,
                [5] = CAMERA_PIN_D5,
                [6] = CAMERA_PIN_D6,
                [7] = CAMERA_PIN_D7,
            },
            .vsync_io = CAMERA_PIN_VSYNC,
            .de_io = CAMERA_PIN_HREF,
            .pclk_io = CAMERA_PIN_PCLK,
            .xclk_io = CAMERA_PIN_XCLK,
        };

        esp_video_init_sccb_config_t sccb_config = {
            .init_sccb = false,
            .i2c_handle = i2c_bus_,
            .freq = 100000,
        };

        esp_video_init_dvp_config_t dvp_config = {
            .sccb_config = sccb_config,
            .reset_pin = CAMERA_PIN_RESET,
            .pwdn_pin = CAMERA_PIN_PWDN,
            .dvp_pin = dvp_pin_config,
            .xclk_freq = XCLK_FREQ_HZ,
        };

        esp_video_init_config_t video_config = {
            .dvp = &dvp_config,
        };

        camera_ = new Esp32Camera(video_config);
        camera_->SetVFlip(true);
        ESP_LOGI(TAG, "OV5640 camera initialized (XCLK on GPIO%d)", CAMERA_PIN_XCLK);
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();

        if (servo_) {
            mcp_server.AddTool("self.servo.shake_head",
                "摇头，水平舵机左右摆动",
                PropertyList(), [this](const PropertyList&) -> ReturnValue {
                ShakeHead();
                return true;
            });
        }

        if (servo_) {
            doa_tracker_ = new DoaTracker(servo_, camera_);

            mcp_server.AddTool("self.sound_tracking.start",
                "启动声源追踪，通过双麦克风定位声源方向并转动舵机",
                PropertyList(), [this](const PropertyList&) -> ReturnValue {
                if (!doa_tracker_) return false;
                doa_tracker_->Start();
                return true;
            });

            mcp_server.AddTool("self.sound_tracking.stop",
                "停止声源追踪，舵机归中",
                PropertyList(), [this](const PropertyList&) -> ReturnValue {
                if (!doa_tracker_) return false;
                doa_tracker_->Stop();
                return true;
            });

            mcp_server.AddTool("self.servo.turn_left",
                "舵机向左转10度",
                PropertyList(), [this](const PropertyList&) -> ReturnValue {
                if (!servo_) return false;
                int16_t pos = servo_->ReadPosition(SERVO_ID);
                if (pos < 0) return false;
                float deg = (float)pos / 4095.0f * 180.0f;
                deg -= 10.0f;
                if (deg < 0) deg = 0;
                uint16_t new_pos = (uint16_t)(deg / 180.0f * 4095);
                servo_->WritePosEx(SERVO_ID, new_pos, 500, 10);
                return true;
            });

            mcp_server.AddTool("self.servo.turn_right",
                "舵机向右转10度",
                PropertyList(), [this](const PropertyList&) -> ReturnValue {
                if (!servo_) return false;
                int16_t pos = servo_->ReadPosition(SERVO_ID);
                if (pos < 0) return false;
                float deg = (float)pos / 4095.0f * 180.0f;
                deg += 10.0f;
                if (deg > 180) deg = 180;
                uint16_t new_pos = (uint16_t)(deg / 180.0f * 4095);
                servo_->WritePosEx(SERVO_ID, new_pos, 500, 10);
                return true;
            });
        }

        ESP_LOGI(TAG, "MCP tools registered (servo=%d, doa=%d)", servo_ ? 1 : 0, doa_tracker_ ? 1 : 0);
    }

    void ShakeHead() {
        if (!servo_) return;

        auto angleToPos = [](float angle) -> uint16_t {
            return (uint16_t)(angle / 180.0f * SERVO_MAX_POS);
        };

        ESP_LOGI(TAG, "Shake head: sweeping 0->180->90");
        servo_->WritePosEx(SERVO_ID, angleToPos(0), 2000, 50);
        vTaskDelay(pdMS_TO_TICKS(1000));
        servo_->WritePosEx(SERVO_ID, angleToPos(180), 2000, 50);
        vTaskDelay(pdMS_TO_TICKS(1000));
        servo_->WritePosEx(SERVO_ID, angleToPos(90), 2000, 50);
        vTaskDelay(pdMS_TO_TICKS(500));

        for (int i = 0; i < 3; i++) {
            servo_->WritePosEx(SERVO_ID, angleToPos(75), 1500, 30);
            vTaskDelay(pdMS_TO_TICKS(300));
            servo_->WritePosEx(SERVO_ID, angleToPos(105), 1500, 30);
            vTaskDelay(pdMS_TO_TICKS(300));
        }
        servo_->WritePosEx(SERVO_ID, angleToPos(90), 1000, 30);
    }

public:
    WaveshareS3CamBoard()
        : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeIoExpander();
        InitializeLcd();
        CreateDoaOverlay();
        InitializeButtons();
        InitializeServo();
        InitializeCamera();
        InitializeTools();
        ESP_LOGI(TAG, "Waveshare ESP32-S3-CAM-OV5640 board ready (STS3215 servo on UART%d, camera active, LCD active)", SERVO_UART_NUM);
    }

    virtual AudioCodec* GetAudioCodec() override {
        static WaveshareS3CamAudioCodec audio_codec(i2c_bus_);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }
};

DECLARE_BOARD(WaveshareS3CamBoard);

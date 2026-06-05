#include "wifi_board.h"
#include "codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "esp32_camera.h"
#include "pca9685_servo.h"
#include "face_tracker.h"
#include "mcp_server.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_touch_ft5x06.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>

#define TAG "XrAiCamBoard"

class XrAiCamAudioCodec : public BoxAudioCodec {
public:
    XrAiCamAudioCodec(i2c_master_bus_handle_t i2c_bus)
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

class XrAiCamBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    LcdDisplay* display_;
    Esp32Camera* camera_;
    Pca9685* pca_ = nullptr;
    FaceTracker* face_tracker_ = nullptr;
    bool preview_mode_ = false;

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
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
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = GPIO_NUM_42;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = GPIO_NUM_41;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeSt7789Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = GPIO_NUM_11;
        io_config.dc_gpio_num = GPIO_NUM_40;
        io_config.spi_mode = 2;
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new SpiLcdDisplay(panel_io, panel,
                                     DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                     DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeTouch() {
        esp_lcd_touch_handle_t tp;
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_WIDTH,
            .y_max = DISPLAY_HEIGHT,
            .rst_gpio_num = GPIO_NUM_NC,
            .int_gpio_num = GPIO_NUM_NC,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 1,
                .mirror_x = 1,
                .mirror_y = 0,
            },
        };
        esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
        tp_io_config.scl_speed_hz = 400000;

        esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle);
        esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp);
        assert(tp);

        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(),
            .handle = tp,
        };
        lvgl_port_add_touch(&touch_cfg);
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
        camera_->SetHMirror(false);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        boot_button_.OnLongPress([this]() {
            if (preview_mode_) {
                camera_->StopPreview();
                preview_mode_ = false;
            } else {
                camera_->StartPreview(display_);
                preview_mode_ = true;
            }
        });
    }

    void InitializeServo() {
        pca_ = new Pca9685(i2c_bus_, PCA9685_I2C_ADDR);
        if (pca_->Init()) {
            pca_->SetPwmFreq(50);
            // Center both servos
            pca_->SetServoAngle(SERVO_CHANNEL_TILT, 90);
            pca_->SetServoAngle(SERVO_CHANNEL_PAN, 90);
            ESP_LOGI(TAG, "PCA9685 servo driver initialized");
        } else {
            ESP_LOGE(TAG, "PCA9685 init failed, servos unavailable");
            delete pca_;
            pca_ = nullptr;
        }
    }

    void InitializeTools() {
        if (!pca_) return;

        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool("self.servo.shake_head", "摇头，水平舵机左右摆动",
            PropertyList(), [this](const PropertyList&) -> ReturnValue {
            ShakeHead();
            return true;
        });

        mcp_server.AddTool("self.servo.nod_head", "点头，俯仰舵机上下摆动",
            PropertyList(), [this](const PropertyList&) -> ReturnValue {
            NodHead();
            return true;
        });

        mcp_server.AddTool("self.servo.dance", "跳舞，两个舵机组合运动",
            PropertyList(), [this](const PropertyList&) -> ReturnValue {
            Dance();
            return true;
        });

        if (face_tracker_) {
            mcp_server.AddTool("self.face_tracking.start", "开始人脸追踪，摄像头自动跟随人脸",
                PropertyList(), [this](const PropertyList&) -> ReturnValue {
                face_tracker_->Start();
                return true;
            });

            mcp_server.AddTool("self.face_tracking.stop", "停止人脸追踪，舵机回到中心",
                PropertyList(), [this](const PropertyList&) -> ReturnValue {
                face_tracker_->Stop();
                return true;
            });
        }

        ESP_LOGI(TAG, "Servo MCP tools registered");
    }

    void ShakeHead() {
        if (!pca_) return;
        ESP_LOGI(TAG, "Shake head");
        for (int i = 0; i < 3; i++) {
            pca_->SetServoAngle(SERVO_CHANNEL_PAN, 75);
            vTaskDelay(pdMS_TO_TICKS(300));
            pca_->SetServoAngle(SERVO_CHANNEL_PAN, 105);
            vTaskDelay(pdMS_TO_TICKS(300));
        }
        pca_->SetServoAngle(SERVO_CHANNEL_PAN, 90);
    }

    void NodHead() {
        if (!pca_) return;
        ESP_LOGI(TAG, "Nod head");
        for (int i = 0; i < 3; i++) {
            pca_->SetServoAngle(SERVO_CHANNEL_TILT, 75);
            vTaskDelay(pdMS_TO_TICKS(300));
            pca_->SetServoAngle(SERVO_CHANNEL_TILT, 105);
            vTaskDelay(pdMS_TO_TICKS(300));
        }
        pca_->SetServoAngle(SERVO_CHANNEL_TILT, 90);
    }

    void Dance() {
        if (!pca_) return;
        ESP_LOGI(TAG, "Dance");
        for (int i = 0; i < 3; i++) {
            // Look left-up
            pca_->SetServoAngle(SERVO_CHANNEL_PAN, 60);
            pca_->SetServoAngle(SERVO_CHANNEL_TILT, 70);
            vTaskDelay(pdMS_TO_TICKS(400));
            // Look right-down
            pca_->SetServoAngle(SERVO_CHANNEL_PAN, 120);
            pca_->SetServoAngle(SERVO_CHANNEL_TILT, 110);
            vTaskDelay(pdMS_TO_TICKS(400));
            // Center bob
            pca_->SetServoAngle(SERVO_CHANNEL_PAN, 90);
            pca_->SetServoAngle(SERVO_CHANNEL_TILT, 70);
            vTaskDelay(pdMS_TO_TICKS(300));
            pca_->SetServoAngle(SERVO_CHANNEL_TILT, 110);
            vTaskDelay(pdMS_TO_TICKS(300));
        }
        // Return to center
        pca_->SetServoAngle(SERVO_CHANNEL_PAN, 90);
        pca_->SetServoAngle(SERVO_CHANNEL_TILT, 90);
    }

public:
    XrAiCamBoard()
        : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeSt7789Display();
        InitializeTouch();
        InitializeButtons();
        InitializeCamera();
        InitializeServo();
        if (pca_) {
            face_tracker_ = new FaceTracker(camera_, pca_, SERVO_CHANNEL_PAN, SERVO_CHANNEL_TILT);
        }
        InitializeTools();
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static XrAiCamAudioCodec audio_codec(i2c_bus_);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }
};

DECLARE_BOARD(XrAiCamBoard);

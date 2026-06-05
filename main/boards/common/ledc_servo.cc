#include "ledc_servo.h"
#include <esp_log.h>
#include <cmath>

#define TAG "LedcServo"

LedcServo::LedcServo(gpio_num_t gpio, ledc_channel_t channel,
                     uint32_t min_pulse_us, uint32_t max_pulse_us)
    : gpio_(gpio), channel_(channel),
      min_pulse_us_(min_pulse_us), max_pulse_us_(max_pulse_us) {}

LedcServo::~LedcServo() {
    if (initialized_) {
        Deinit();
    }
}

bool LedcServo::Init() {
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_14_BIT,
        .timer_num = LEDC_TIMER_1,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
        return false;
    }

    ledc_channel_config_t ch_cfg = {
        .gpio_num = gpio_,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = channel_,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_1,
        .duty = 0,
        .hpoint = 0,
    };
    ret = ledc_channel_config(&ch_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(ret));
        return false;
    }

    SetAngle(90.0f);
    initialized_ = true;
    ESP_LOGI(TAG, "LEDC servo initialized on GPIO%d (ch=%d, 50Hz)", gpio_, channel_);
    return true;
}

void LedcServo::Deinit() {
    if (!initialized_) return;
    ledc_stop(LEDC_LOW_SPEED_MODE, channel_, 0);
    gpio_reset_pin(gpio_);
    initialized_ = false;
    ESP_LOGI(TAG, "LEDC servo deinitialized on GPIO%d", gpio_);
}

void LedcServo::SetAngle(float angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    current_angle_ = angle;

    // Map angle to pulse width
    float pulse_us = min_pulse_us_ + (angle / 180.0f) * (max_pulse_us_ - min_pulse_us_);

    // Convert pulse width to duty cycle: duty = pulse_us / period_us * 2^resolution
    // At 50Hz, period = 20000us. duty = pulse_us / 20000us * 16384
    uint32_t duty = (uint32_t)(pulse_us * 16384.0f / 20000.0f);

    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel_, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel_);
}

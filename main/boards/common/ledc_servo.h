#pragma once

#include <driver/gpio.h>
#include <driver/ledc.h>

class LedcServo {
public:
    LedcServo(gpio_num_t gpio, ledc_channel_t channel,
              uint32_t min_pulse_us = 500, uint32_t max_pulse_us = 2500);
    ~LedcServo();

    bool Init();
    void Deinit();
    void SetAngle(float angle);
    float GetAngle() const { return current_angle_; }

private:
    gpio_num_t gpio_;
    ledc_channel_t channel_;
    uint32_t min_pulse_us_;
    uint32_t max_pulse_us_;
    float current_angle_ = 90.0f;
    bool initialized_ = false;
};

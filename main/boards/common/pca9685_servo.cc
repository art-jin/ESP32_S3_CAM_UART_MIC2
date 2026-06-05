#include "pca9685_servo.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cmath>

#define TAG "PCA9685"

// PCA9685 registers
constexpr uint8_t REG_MODE1 = 0x00;
constexpr uint8_t REG_MODE2 = 0x01;
constexpr uint8_t REG_PRESCALE = 0xFE;
constexpr uint8_t REG_LED0_ON_L = 0x06;

Pca9685::Pca9685(i2c_master_bus_handle_t i2c_bus, uint8_t addr)
    : i2c_bus_(i2c_bus), addr_(addr) {}

Pca9685::~Pca9685() {
    if (dev_handle_) {
        i2c_master_bus_rm_device(dev_handle_);
    }
}

bool Pca9685::Init() {
    esp_err_t probe_ret = i2c_master_probe(i2c_bus_, addr_, pdMS_TO_TICKS(500));
    if (probe_ret != ESP_OK) {
        ESP_LOGE(TAG, "No PCA9685 at address 0x%02X: %s", addr_, esp_err_to_name(probe_ret));
        return false;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr_,
        .scl_speed_hz = 100000,
    };
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus_, &dev_cfg, &dev_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device: %s", esp_err_to_name(ret));
        return false;
    }

    // Reset
    if (!WriteRegister(REG_MODE1, 0x00)) return false;

    // Output logic: totem-pole (push-pull) for servo driving
    if (!WriteRegister(REG_MODE2, 0x04)) return false;

    ESP_LOGI(TAG, "PCA9685 initialized at 0x%02X", addr_);
    return true;
}

bool Pca9685::Reinit() {
    if (!dev_handle_) return false;

    // Full reset
    if (!WriteRegister(REG_MODE1, 0x00)) return false;
    vTaskDelay(pdMS_TO_TICKS(1));

    // Totem-pole output
    if (!WriteRegister(REG_MODE2, 0x04)) return false;

    // Set 50Hz frequency (write-only: avoids read conflict with shared-address devices)
    uint8_t prescale = (uint8_t)std::round(25000000.0f / 4096.0f / 50.0f - 1.0f);
    if (!WriteRegister(REG_MODE1, 0x10)) return false;  // sleep
    if (!WriteRegister(REG_PRESCALE, prescale)) return false;
    if (!WriteRegister(REG_MODE1, 0x00)) return false;   // wake
    vTaskDelay(pdMS_TO_TICKS(5));
    if (!WriteRegister(REG_MODE1, 0x20)) return false;   // auto-increment

    ESP_LOGI(TAG, "PCA9685 re-initialized (50Hz, write-only)");
    return true;
}

bool Pca9685::SetPwmFreq(uint16_t freq) {
    float prescaleval = 25000000.0f / 4096.0f / freq - 1.0f;
    uint8_t prescale = (uint8_t)std::round(prescaleval);

    uint8_t oldmode;
    if (!ReadRegister(REG_MODE1, &oldmode)) return false;

    uint8_t sleepmode = (oldmode & 0x7F) | 0x10;
    if (!WriteRegister(REG_MODE1, sleepmode)) return false;
    if (!WriteRegister(REG_PRESCALE, prescale)) return false;
    if (!WriteRegister(REG_MODE1, oldmode)) return false;

    // Wait for oscillator to stabilize
    vTaskDelay(pdMS_TO_TICKS(5));

    // Enable auto-increment
    if (!WriteRegister(REG_MODE1, oldmode | 0x20)) return false;

    ESP_LOGI(TAG, "PWM freq set to %d Hz (prescale=%d)", freq, prescale);
    return true;
}

bool Pca9685::SetServoAngle(uint8_t channel, float angle) {
    // Reference test code uses 150-600 ticks for servo range
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;

    uint16_t ticks = (uint16_t)(150 + (angle / 180.0f) * 450);

    // Retry up to 3 times on I2C failure (bus contention with audio codecs)
    for (int i = 0; i < 3; i++) {
        if (SetPwm(channel, 0, ticks)) return true;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return false;
}

bool Pca9685::SetPwm(uint8_t channel, uint16_t on, uint16_t off) {
    uint8_t reg = REG_LED0_ON_L + channel * 4;
    uint8_t data[4] = {
        (uint8_t)(on & 0xFF),
        (uint8_t)(on >> 8),
        (uint8_t)(off & 0xFF),
        (uint8_t)(off >> 8),
    };
    return WriteRegisters(reg, data, 4);
}

bool Pca9685::WriteRegister(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    esp_err_t ret = i2c_master_transmit(dev_handle_, buf, 2, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
        return false;
    }
    return true;
}

bool Pca9685::WriteRegisters(uint8_t reg, const uint8_t* data, size_t len) {
    uint8_t buf[1 + len];
    buf[0] = reg;
    memcpy(&buf[1], data, len);
    esp_err_t ret = i2c_master_transmit(dev_handle_, buf, 1 + len, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write regs from 0x%02X len=%d failed: %s", reg, (int)len, esp_err_to_name(ret));
        return false;
    }
    return true;
}

bool Pca9685::ReadRegister(uint8_t reg, uint8_t* value) {
    esp_err_t ret = i2c_master_transmit_receive(dev_handle_, &reg, 1, value, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
        return false;
    }
    return true;
}

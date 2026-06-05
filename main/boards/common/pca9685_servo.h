#pragma once

#include <driver/i2c_master.h>

class Pca9685 {
public:
    Pca9685(i2c_master_bus_handle_t i2c_bus, uint8_t addr = 0x38);
    ~Pca9685();

    bool Init();
    // Write-only reinit — safe when another device shares the I2C address
    bool Reinit();
    bool SetPwmFreq(uint16_t freq);
    bool SetServoAngle(uint8_t channel, float angle);
    bool SetPwm(uint8_t channel, uint16_t on, uint16_t off);
    bool ReadRegister(uint8_t reg, uint8_t* value);

private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t dev_handle_ = nullptr;
    uint8_t addr_;

    bool WriteRegister(uint8_t reg, uint8_t value);
    bool WriteRegisters(uint8_t reg, const uint8_t* data, size_t len);
};

#pragma once

#include <cstdint>
#include <driver/gpio.h>
#include <driver/uart.h>

// STS instruction codes
constexpr uint8_t STS_INST_PING = 0x01;
constexpr uint8_t STS_INST_READ = 0x02;
constexpr uint8_t STS_INST_WRITE = 0x03;
constexpr uint8_t STS_INST_REG_WRITE = 0x04;
constexpr uint8_t STS_INST_REG_ACTION = 0x05;
constexpr uint8_t STS_INST_SYNC_WRITE = 0x83;

// STS register addresses
constexpr uint8_t STS_REG_TORQUE_ENABLE = 40;
constexpr uint8_t STS_REG_ACC = 41;
constexpr uint8_t STS_REG_GOAL_POSITION_L = 42;
constexpr uint8_t STS_REG_GOAL_POSITION_H = 43;
constexpr uint8_t STS_REG_GOAL_TIME_L = 44;
constexpr uint8_t STS_REG_GOAL_TIME_H = 45;
constexpr uint8_t STS_REG_GOAL_SPEED_L = 46;
constexpr uint8_t STS_REG_GOAL_SPEED_H = 47;
constexpr uint8_t STS_REG_PRESENT_POSITION_L = 56;
constexpr uint8_t STS_REG_PRESENT_POSITION_H = 57;
constexpr uint8_t STS_REG_PRESENT_SPEED_L = 58;
constexpr uint8_t STS_REG_PRESENT_SPEED_H = 59;
constexpr uint8_t STS_REG_PRESENT_LOAD_L = 60;
constexpr uint8_t STS_REG_PRESENT_LOAD_H = 61;
constexpr uint8_t STS_REG_PRESENT_VOLTAGE = 62;
constexpr uint8_t STS_REG_PRESENT_TEMPERATURE = 63;
constexpr uint8_t STS_REG_MOVING = 66;

constexpr uint8_t STS_BROADCAST_ID = 0xFE;
constexpr uint16_t STS_CENTER_POSITION = 2047;
constexpr uint16_t STS_MAX_POSITION = 4095;

class StsServo {
public:
    StsServo(uart_port_t uart_num, gpio_num_t tx_pin, gpio_num_t rx_pin, int baud_rate = 1000000);
    ~StsServo();

    bool Ping(uint8_t id);
    bool EnableTorque(uint8_t id, bool enable);
    bool WritePosEx(uint8_t id, uint16_t position, uint16_t speed = 0, uint8_t acc = 0);
    int16_t ReadPosition(uint8_t id);
    bool ReadVoltage(uint8_t id, uint8_t& voltage);
    bool ReadTemperature(uint8_t id, uint8_t& temp);
    bool IsMoving(uint8_t id);

private:
    uart_port_t uart_num_;
    gpio_num_t tx_pin_;
    gpio_num_t rx_pin_;

    void SendPacket(uint8_t id, uint8_t instruction, const uint8_t* params, uint8_t param_len);
    int ReceivePacket(uint8_t* buf, int buf_len, int timeout_ms = 50);
    void FlushRx();
    uint8_t Checksum(uint8_t id, uint8_t length, const uint8_t* data, int data_len);
    bool WriteRegisters(uint8_t id, uint8_t start_reg, const uint8_t* data, uint8_t len);
    int ReadRegisters(uint8_t id, uint8_t start_reg, uint8_t len, uint8_t* out);
};

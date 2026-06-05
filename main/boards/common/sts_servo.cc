#include "sts_servo.h"
#include <esp_log.h>
#include <driver/uart.h>
#include <cstring>

#define TAG "StsServo"

StsServo::StsServo(uart_port_t uart_num, gpio_num_t tx_pin, gpio_num_t rx_pin, int baud_rate)
    : uart_num_(uart_num), tx_pin_(tx_pin), rx_pin_(rx_pin) {
    uart_config_t uart_config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(uart_num_, 256, 256, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(uart_num_, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num_, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "UART%d initialized: TX=%d, RX=%d, baud=%d", uart_num_, tx_pin, rx_pin, baud_rate);
}

StsServo::~StsServo() {
    uart_driver_delete(uart_num_);
}

void StsServo::FlushRx() {
    uart_flush_input(uart_num_);
}

uint8_t StsServo::Checksum(uint8_t id, uint8_t length, const uint8_t* data, int data_len) {
    uint32_t sum = id + length;
    for (int i = 0; i < data_len; i++) {
        sum += data[i];
    }
    return ~(sum) & 0xFF;
}

void StsServo::SendPacket(uint8_t id, uint8_t instruction, const uint8_t* params, uint8_t param_len) {
    uint8_t length = param_len + 2; // instruction + params + checksum
    uint8_t pkt[6 + param_len];
    pkt[0] = 0xFF;
    pkt[1] = 0xFF;
    pkt[2] = id;
    pkt[3] = length;
    pkt[4] = instruction;
    if (param_len > 0 && params) {
        memcpy(&pkt[5], params, param_len);
    }
    // Build checksum data: instruction + params
    uint8_t check_data[1 + param_len];
    check_data[0] = instruction;
    if (param_len > 0 && params) {
        memcpy(&check_data[1], params, param_len);
    }
    pkt[5 + param_len] = Checksum(id, length, check_data, 1 + param_len);

    FlushRx();
    int written = uart_write_bytes(uart_num_, pkt, 6 + param_len);
    uart_wait_tx_done(uart_num_, pdMS_TO_TICKS(50));
    ESP_LOGD(TAG, "TX %d bytes: ID=%d INST=0x%02X", written, id, instruction);
}

int StsServo::ReceivePacket(uint8_t* buf, int buf_len, int timeout_ms) {
    int total = 0;
    // Read header (2 bytes)
    int n = uart_read_bytes(uart_num_, buf, 2, pdMS_TO_TICKS(100));
    if (n < 2 || buf[0] != 0xFF || buf[1] != 0xFF) {
        return 0;
    }
    total = 2;

    // Read ID + Length (2 bytes)
    n = uart_read_bytes(uart_num_, &buf[2], 2, pdMS_TO_TICKS(50));
    if (n < 2) return 0;
    total += 2;

    uint8_t length = buf[3];
    if (length > buf_len - 4) return 0;

    // Read remaining bytes (length bytes = instruction/params + checksum)
    n = uart_read_bytes(uart_num_, &buf[4], length, pdMS_TO_TICKS(50));
    if (n < length) return 0;
    total += length;

    // Verify checksum
    uint8_t check = Checksum(buf[2], buf[3], &buf[4], length);
    if (check != 0) {
        ESP_LOGW(TAG, "Checksum mismatch: got 0x%02X, expected 0x00", check);
        return 0;
    }

    return total;
}

bool StsServo::Ping(uint8_t id) {
    SendPacket(id, STS_INST_PING, nullptr, 0);

    // Debug: check if any bytes come back at all
    uint8_t raw[64];
    int raw_len = uart_read_bytes(uart_num_, raw, sizeof(raw), pdMS_TO_TICKS(200));
    if (raw_len > 0) {
        ESP_LOGI(TAG, "Ping RX raw %d bytes:", raw_len);
        ESP_LOG_BUFFER_HEX(TAG, raw, raw_len);
    } else {
        ESP_LOGW(TAG, "Ping ID=%d: no response (0 bytes received)", id);
        return false;
    }

    // Try to parse as STS response
    if (raw_len >= 6 && raw[0] == 0xFF && raw[1] == 0xFF) {
        ESP_LOGI(TAG, "Ping ID=%d: OK (status=0x%02X)", id, raw[4]);
        return true;
    }
    return false;
}

bool StsServo::EnableTorque(uint8_t id, bool enable) {
    uint8_t data = enable ? 1 : 0;
    return WriteRegisters(id, STS_REG_TORQUE_ENABLE, &data, 1);
}

bool StsServo::WritePosEx(uint8_t id, uint16_t position, uint16_t speed, uint8_t acc) {
    // Write 7 bytes starting at ACC register: acc, posL, posH, timeL, timeH, speedL, speedH
    uint8_t params[8];
    params[0] = STS_REG_ACC; // start register
    params[1] = acc;
    params[2] = position & 0xFF;
    params[3] = (position >> 8) & 0xFF;
    params[4] = 0; // time L (0 = no time limit)
    params[5] = 0; // time H
    params[6] = speed & 0xFF;
    params[7] = (speed >> 8) & 0xFF;

    SendPacket(id, STS_INST_WRITE, params, 8);

    // Write doesn't usually get a response unless we explicitly check
    // but the servo does respond with status
    return true;
}

int16_t StsServo::ReadPosition(uint8_t id) {
    uint8_t data[2];
    int len = ReadRegisters(id, STS_REG_PRESENT_POSITION_L, 2, data);
    if (len < 2) return -1;
    return (int16_t)(data[0] | (data[1] << 8));
}

bool StsServo::ReadVoltage(uint8_t id, uint8_t& voltage) {
    uint8_t data[1];
    int len = ReadRegisters(id, STS_REG_PRESENT_VOLTAGE, 1, data);
    if (len < 1) return false;
    voltage = data[0];
    return true;
}

bool StsServo::ReadTemperature(uint8_t id, uint8_t& temp) {
    uint8_t data[1];
    int len = ReadRegisters(id, STS_REG_PRESENT_TEMPERATURE, 1, data);
    if (len < 1) return false;
    temp = data[0];
    return true;
}

bool StsServo::IsMoving(uint8_t id) {
    uint8_t data[1];
    int len = ReadRegisters(id, STS_REG_MOVING, 1, data);
    if (len < 1) return false;
    return data[0] != 0;
}

bool StsServo::WriteRegisters(uint8_t id, uint8_t start_reg, const uint8_t* data, uint8_t len) {
    uint8_t params[1 + len];
    params[0] = start_reg;
    memcpy(&params[1], data, len);
    SendPacket(id, STS_INST_WRITE, params, 1 + len);
    return true;
}

int StsServo::ReadRegisters(uint8_t id, uint8_t start_reg, uint8_t len, uint8_t* out) {
    uint8_t params[2] = {start_reg, len};
    SendPacket(id, STS_INST_READ, params, 2);

    uint8_t resp[64];
    int resp_len = ReceivePacket(resp, sizeof(resp));
    if (resp_len < 6) return 0;

    // Response: FF FF ID Length Error Data... Checksum
    // Length = error + data_len + checksum = data_len + 2
    uint8_t data_len = resp[3] - 2; // subtract error byte + checksum
    if (data_len > len) data_len = len;
    memcpy(out, &resp[5], data_len);
    return data_len;
}

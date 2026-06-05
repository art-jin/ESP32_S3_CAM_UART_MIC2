#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdint.h>

class Esp32Camera;
class Pca9685;

class FaceTracker {
public:
    FaceTracker(Esp32Camera* camera, Pca9685* pca,
                uint8_t pan_ch, uint8_t tilt_ch);
    ~FaceTracker();

    bool Start();
    void Stop();
    bool IsRunning() const;

private:
    Esp32Camera* camera_;
    Pca9685* pca_;
    uint8_t pan_ch_;
    uint8_t tilt_ch_;
    float pan_angle_ = 90;
    float tilt_angle_ = 90;
    int lost_count_ = 0;

    uint8_t* full_buf_ = nullptr;
    uint8_t* ds_buf_ = nullptr;

    TaskHandle_t task_ = nullptr;
    volatile bool running_ = false;

    static constexpr uint16_t DS_WIDTH = 320;
    static constexpr uint16_t DS_HEIGHT = 240;
    static constexpr float KP = 0.4f;
    static constexpr int DEAD_ZONE = 10;
    static constexpr float MAX_STEP = 3.0f;
    static constexpr float SMOOTH_ALPHA = 0.3f;
    static constexpr int LOST_LIMIT = 30;
    static constexpr float ANGLE_MIN = 30.0f;
    static constexpr float ANGLE_MAX = 150.0f;

    void TaskLoop();
    static void TaskEntry(void* arg);
    void DownsampleRGB565(const uint8_t* src, uint16_t src_w, uint16_t src_h,
                          uint8_t* dst, uint16_t dst_w, uint16_t dst_h);
    void UpdateServos(int cx, int cy, int img_w, int img_h);
    void ReturnToCenter();
};

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <freertos/semphr.h>
#include <stdint.h>
#include <cstddef>
#include <functional>

class StsServo;
class Camera;

class DoaTracker {
public:
    DoaTracker(StsServo* servo, Camera* camera,
               std::function<float(int16_t)> pos_to_deg = nullptr,
               std::function<uint16_t(float)> deg_to_pos = nullptr,
               float angle_min = 10.0f,
               float angle_max = 170.0f,
               float center_deg = 90.0f);
    ~DoaTracker();

    void Start();
    void Stop();
    bool IsRunning() const;

    // Called from audio thread — non-blocking, drops data if buffer full
    void FeedAudio(const int16_t* data, size_t frames);

    struct DebugInfo {
        float angle_offset;
        float ch0_energy;
        float ch1_energy;
        float correlation;
        bool is_valid;
        bool is_running;
        int valid_count;
        int frame_count;
        float noise_floor;
        float snr;
    };
    DebugInfo GetDebugInfo() const;

    // Static accessor for AudioService to feed data
    static DoaTracker* GetInstance();

private:
    StsServo* servo_;
    Camera* camera_;

    // Injectable angle conversion (board-specific gear ratio)
    std::function<float(int16_t)> pos_to_deg_;
    std::function<uint16_t(float)> deg_to_pos_;
    float center_deg_;
    float angle_min_;
    float angle_max_;

    RingbufHandle_t ring_buf_ = nullptr;

    // FreeRTOS task
    TaskHandle_t task_ = nullptr;
    volatile bool running_ = false;
    float current_angle_ = 0.0f;

    // GCC-PHAT buffers (allocated in Start)
    float last_max_val_ = 0;
    float* buf_ch0_ = nullptr;
    float* buf_ch1_ = nullptr;
    float* buf_cross_ = nullptr;

    // Noise floor tracking
    float noise_floor_ = 100.0f;
    int noise_calib_count_ = 0;
    static constexpr int NOISE_CALIB_FRAMES = 50;    // frames to calibrate noise floor at start
    static constexpr float NOISE_LEARN_RATE = 0.005f; // slow adaptation for noise floor
    static constexpr float SNR_THRESHOLD = 2.5f;      // signal must be 2.5x noise floor

    // Peak hold for display
    float peak_angle_ = 0;
    int peak_hold_frames_ = 0;
    static constexpr int PEAK_HOLD_DURATION = 25;  // hold last peak for ~0.8s (25 frames * 32ms)

    // Auto-stop: stop tracking after this many frames without valid detection
    int no_valid_count_ = 0;
    static constexpr int AUTO_STOP_FRAMES = 900;  // ~30s at ~33ms/frame

    // Debug info (atomic-ish, written by task, read by LCD)
    volatile float dbg_angle_ = 0;
    volatile float dbg_ch0_energy_ = 0;
    volatile float dbg_ch1_energy_ = 0;
    volatile float dbg_correlation_ = 0;
    volatile bool dbg_valid_ = false;
    volatile int dbg_valid_count_ = 0;
    volatile int dbg_frame_count_ = 0;
    volatile float dbg_noise_floor_ = 0;
    volatile float dbg_snr_ = 0;

    bool AllocBuffers();
    void FreeBuffers();

    float EstimateDirectionFloat();

    void UpdateServo(float angle);
    void ServoToCenter();

    // Sliding window for direction estimates
    static constexpr int WINDOW_SIZE = 15;
    float estimates_[WINDOW_SIZE] = {};
    bool valid_[WINDOW_SIZE] = {};
    int est_idx_ = 0;
    int est_count_ = 0;
    void PushEstimate(float angle, bool valid);
    float GetConsistentEstimate();

    float ch0_energy_ = 0;
    float ch1_energy_ = 0;

    static constexpr size_t FFT_SIZE = 512;
    static constexpr float MIC_SPACING_M = 0.04f;
    static constexpr float SPEED_OF_SOUND = 343.0f;
    static constexpr float SAMPLE_RATE = 24000.0f;
    static constexpr float SMOOTH_ALPHA = 0.8f;
    static constexpr int DEAD_ZONE_DEG = 3;
    static constexpr int CONVERGE_COUNT = 2;
    static constexpr float MIN_CORRELATION = 0.20f;  // lower threshold — 4cm spacing produces weaker peaks
    static constexpr int UPDATE_INTERVAL = 5;
    static constexpr int SERVO_MIN_INTERVAL_MS = 200;

    // Global instance for audio data feeding
    static DoaTracker* instance_;
    static SemaphoreHandle_t instance_mutex_;

    static void TaskEntry(void* arg);
    void TaskLoop();
};

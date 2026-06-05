#include "doa_tracker.h"
#include "sts_servo.h"
#include "camera.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <freertos/ringbuf.h>
#include <dsps_fft2r.h>
#include <cmath>
#include <algorithm>

#define TAG "DoaTracker"

DoaTracker* DoaTracker::instance_ = nullptr;
SemaphoreHandle_t DoaTracker::instance_mutex_ = nullptr;

DoaTracker::DoaTracker(StsServo* servo, Camera* camera)
    : servo_(servo), camera_(camera) {
    instance_mutex_ = xSemaphoreCreateMutex();
}

DoaTracker::~DoaTracker() {
    Stop();
    FreeBuffers();
    if (instance_mutex_) {
        vSemaphoreDelete(instance_mutex_);
        instance_mutex_ = nullptr;
    }
}

DoaTracker* DoaTracker::GetInstance() {
    return instance_;
}

DoaTracker::DebugInfo DoaTracker::GetDebugInfo() const {
    return {
        dbg_angle_, dbg_ch0_energy_, dbg_ch1_energy_,
        dbg_correlation_, dbg_valid_, running_,
        dbg_valid_count_, dbg_frame_count_,
        dbg_noise_floor_, dbg_snr_
    };
}

void DoaTracker::Start() {
    if (running_) return;

    if (!AllocBuffers()) {
        ESP_LOGE(TAG, "Failed to allocate DOA buffers");
        return;
    }

    est_idx_ = 0;
    est_count_ = 0;
    no_valid_count_ = 0;
    ch0_energy_ = 0;
    ch1_energy_ = 0;
    dbg_frame_count_ = 0;
    noise_floor_ = 100.0f;
    noise_calib_count_ = 0;
    peak_angle_ = 0;
    peak_hold_frames_ = 0;
    for (int i = 0; i < WINDOW_SIZE; i++) valid_[i] = false;
    running_ = true;

    xSemaphoreTake(instance_mutex_, portMAX_DELAY);
    instance_ = this;
    xSemaphoreGive(instance_mutex_);

    BaseType_t ret = xTaskCreatePinnedToCore(
        TaskEntry, "doa_track", 8192, this, 4, &task_, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DOA task");
        running_ = false;
        instance_ = nullptr;
        FreeBuffers();
        return;
    }

    ESP_LOGI(TAG, "DOA tracking started (FFT=%d, mic_spacing=%.1fcm, sr=%.0f)",
             (int)FFT_SIZE, MIC_SPACING_M * 100, SAMPLE_RATE);
}

void DoaTracker::Stop() {
    if (!running_) return;
    running_ = false;

    xSemaphoreTake(instance_mutex_, portMAX_DELAY);
    instance_ = nullptr;
    xSemaphoreGive(instance_mutex_);

    // Wait for task to exit gracefully (it frees its own audio_buf)
    vTaskDelay(pdMS_TO_TICKS(300));
    if (task_) {
        // Safety: delete only if task didn't exit on its own
        vTaskDelete(task_);
        task_ = nullptr;
    }

    FreeBuffers();
    ServoToCenter();
    ESP_LOGI(TAG, "DOA tracking stopped, servo centered");
}

bool DoaTracker::IsRunning() const {
    return running_;
}

bool DoaTracker::AllocBuffers() {
    ring_buf_ = xRingbufferCreate(FFT_SIZE * 2 * sizeof(int16_t) * 8, RINGBUF_TYPE_BYTEBUF);
    if (!ring_buf_) {
        ESP_LOGE(TAG, "Ring buffer alloc failed");
        return false;
    }

    esp_err_t ret = dsps_fft2r_init_fc32(NULL, FFT_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FFT init failed: %s", esp_err_to_name(ret));
        return false;
    }

    size_t complex_size = FFT_SIZE * 2 * sizeof(float);
    buf_ch0_ = (float*)heap_caps_malloc(complex_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    buf_ch1_ = (float*)heap_caps_malloc(complex_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    buf_cross_ = (float*)heap_caps_malloc(complex_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    return buf_ch0_ && buf_ch1_ && buf_cross_;
}

void DoaTracker::FreeBuffers() {
    if (buf_ch0_) { heap_caps_free(buf_ch0_); buf_ch0_ = nullptr; }
    if (buf_ch1_) { heap_caps_free(buf_ch1_); buf_ch1_ = nullptr; }
    if (buf_cross_) { heap_caps_free(buf_cross_); buf_cross_ = nullptr; }
    if (ring_buf_) { vRingbufferDelete(ring_buf_); ring_buf_ = nullptr; }
}

void DoaTracker::FeedAudio(const int16_t* data, size_t frames) {
    if (!running_ || !ring_buf_) return;
    xRingbufferSend(ring_buf_, data, frames * 2 * sizeof(int16_t), 0);
}

void DoaTracker::TaskEntry(void* arg) {
    static_cast<DoaTracker*>(arg)->TaskLoop();
}

void DoaTracker::TaskLoop() {
    size_t needed_bytes = FFT_SIZE * 2 * sizeof(int16_t);
    int16_t* audio_buf = (int16_t*)heap_caps_malloc(needed_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!audio_buf) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        running_ = false;
        return;
    }

    ESP_LOGI(TAG, "TaskLoop started, waiting for audio...");

    size_t accumulated = 0;
    int frame_count = 0;
    int no_data_count = 0;

    while (running_) {
        size_t item_size = 0;
        uint8_t* item = (uint8_t*)xRingbufferReceiveUpTo(
            ring_buf_, &item_size, pdMS_TO_TICKS(200), needed_bytes - accumulated);

        if (!item) {
            no_data_count++;
            if (no_data_count == 10) {
                ESP_LOGW(TAG, "No audio data received after 2s (accumulated=%d)", (int)accumulated);
            }
            if (accumulated == 0) continue;
            continue;
        }

        no_data_count = 0;
        size_t copy_len = std::min(item_size, needed_bytes - accumulated);
        memcpy((uint8_t*)audio_buf + accumulated, item, copy_len);
        vRingbufferReturnItem(ring_buf_, item);
        accumulated += copy_len;

        if (accumulated < needed_bytes) continue;

        accumulated = 0;
        frame_count++;
        dbg_frame_count_ = frame_count;

        if (frame_count == 1) {
            ESP_LOGI(TAG, "First frame! CH0[0..4]=[%d,%d,%d,%d,%d] CH1[0..4]=[%d,%d,%d,%d,%d]",
                     audio_buf[0], audio_buf[2], audio_buf[4], audio_buf[6], audio_buf[8],
                     audio_buf[1], audio_buf[3], audio_buf[5], audio_buf[7], audio_buf[9]);
        }

        // Deinterleave and convert to complex float
        float e0 = 0, e1 = 0;
        for (size_t i = 0; i < FFT_SIZE; i++) {
            float s0 = (float)audio_buf[i * 2];
            float s1 = (float)audio_buf[i * 2 + 1];
            buf_ch0_[i * 2] = s0;
            buf_ch0_[i * 2 + 1] = 0.0f;
            buf_ch1_[i * 2] = s1;
            buf_ch1_[i * 2 + 1] = 0.0f;
            e0 += s0 * s0;
            e1 += s1 * s1;
        }
        ch0_energy_ = e0 / FFT_SIZE;
        ch1_energy_ = e1 / FFT_SIZE;
        float avg_energy = (ch0_energy_ + ch1_energy_) / 2.0f;

        float angle = EstimateDirectionFloat();
        float max_val = last_max_val_;

        // Noise floor tracking
        if (noise_calib_count_ < NOISE_CALIB_FRAMES) {
            // Initial calibration: build up noise floor estimate
            noise_floor_ += (avg_energy - noise_floor_) * 0.1f;
            noise_calib_count_++;
        } else {
            // Slow adaptation: only update noise floor when signal is low (likely noise)
            if (avg_energy < noise_floor_ * 2.0f) {
                noise_floor_ += (avg_energy - noise_floor_) * NOISE_LEARN_RATE;
            }
        }
        if (noise_floor_ < 10.0f) noise_floor_ = 10.0f;
        if (noise_floor_ > 2000.0f) noise_floor_ = 2000.0f;

        float snr = avg_energy / noise_floor_;

        // Valid detection: high SNR + good correlation
        bool is_valid = (snr > SNR_THRESHOLD && max_val > MIN_CORRELATION);

        // Update debug info for LCD display
        dbg_ch0_energy_ = ch0_energy_;
        dbg_ch1_energy_ = ch1_energy_;
        dbg_correlation_ = max_val;
        dbg_valid_ = is_valid;
        dbg_noise_floor_ = noise_floor_;
        dbg_snr_ = snr;

        // Peak hold: keep showing last strong detection
        if (is_valid) {
            peak_angle_ = angle;
            peak_hold_frames_ = PEAK_HOLD_DURATION;
            dbg_angle_ = angle;
            no_valid_count_ = 0;
        } else if (peak_hold_frames_ > 0) {
            peak_hold_frames_--;
            // Keep showing the peak angle
        } else {
            dbg_angle_ = 0;  // no signal, show center
        }

        // Auto-stop: no valid detection for too long
        if (!is_valid) {
            no_valid_count_++;
        }
        if (no_valid_count_ >= AUTO_STOP_FRAMES) {
            ESP_LOGI(TAG, "Auto-stop: no valid detection for %d frames (~%ds)", no_valid_count_, no_valid_count_ * 33 / 1000);
            running_ = false;
            break;
        }

        PushEstimate(angle, is_valid);
        dbg_valid_count_ = 0;
        for (int i = 0; i < est_count_ && i < WINDOW_SIZE; i++) {
            if (valid_[i]) dbg_valid_count_++;
        }

        if (frame_count <= 10 || frame_count % 50 == 0) {
            ESP_LOGI(TAG, "F%d: off=%.1f v=%d snr=%.1f noise=%.0f e=%.0f corr=%.2f",
                     frame_count, angle, is_valid, snr, noise_floor_, avg_energy, max_val);
        }

        // Only update servo with consistent direction readings
        if (is_valid && frame_count % UPDATE_INTERVAL == 0 && est_count_ >= CONVERGE_COUNT) {
            float consistent = GetConsistentEstimate();
            if (consistent >= -90.0f) {
                UpdateServo(consistent);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    heap_caps_free(audio_buf);
}

float DoaTracker::EstimateDirectionFloat() {
    dsps_fft2r_fc32(buf_ch0_, FFT_SIZE);
    dsps_fft2r_fc32(buf_ch1_, FFT_SIZE);
    dsps_bit_rev_fc32(buf_ch0_, FFT_SIZE);
    dsps_bit_rev_fc32(buf_ch1_, FFT_SIZE);

    // Cross-power spectrum with PHAT weighting
    for (int i = 0; i < FFT_SIZE; i++) {
        float re1 = buf_ch0_[i * 2];
        float im1 = buf_ch0_[i * 2 + 1];
        float re2 = buf_ch1_[i * 2];
        float im2 = buf_ch1_[i * 2 + 1];

        float cross_re = re1 * re2 + im1 * im2;
        float cross_im = re1 * im2 - im1 * re2;

        float mag = sqrtf(cross_re * cross_re + cross_im * cross_im);
        if (mag < 1e-10f) mag = 1e-10f;

        buf_cross_[i * 2] = cross_re / mag;
        buf_cross_[i * 2 + 1] = cross_im / mag;
    }

    // IFFT
    for (int i = 0; i < FFT_SIZE; i++) {
        buf_cross_[i * 2 + 1] = -buf_cross_[i * 2 + 1];
    }
    dsps_fft2r_fc32(buf_cross_, FFT_SIZE);
    dsps_bit_rev_fc32(buf_cross_, FFT_SIZE);
    float inv_n = 1.0f / FFT_SIZE;
    for (int i = 0; i < FFT_SIZE; i++) {
        buf_cross_[i * 2] *= inv_n;
        buf_cross_[i * 2 + 1] *= -inv_n;
    }

    // Find peak lag
    int max_lag = (int)(MIC_SPACING_M * SAMPLE_RATE / SPEED_OF_SOUND) + 2;
    if (max_lag > (int)(FFT_SIZE / 2 - 1)) max_lag = FFT_SIZE / 2 - 1;

    float max_val = -1e30f;
    int best_lag = 0;

    for (int lag = -max_lag; lag <= max_lag; lag++) {
        int idx = lag >= 0 ? lag : FFT_SIZE + lag;
        float val = buf_cross_[idx * 2];
        if (val > max_val) {
            max_val = val;
            best_lag = lag;
        }
    }

    // Parabolic interpolation
    float refined_lag = (float)best_lag;
    int idx_curr = best_lag >= 0 ? best_lag : FFT_SIZE + best_lag;
    int idx_prev = -1, idx_next = -1;
    if (best_lag > -max_lag) {
        int p = best_lag - 1;
        idx_prev = p >= 0 ? p : FFT_SIZE + p;
    }
    if (best_lag < max_lag) {
        idx_next = (best_lag + 1) % FFT_SIZE;
    }
    if (idx_prev >= 0 && idx_next >= 0) {
        float y_prev = buf_cross_[idx_prev * 2];
        float y_curr = buf_cross_[idx_curr * 2];
        float y_next = buf_cross_[idx_next * 2];
        float denom = 2.0f * (2.0f * y_curr - y_prev - y_next);
        if (fabsf(denom) > 1e-10f) {
            refined_lag = (float)best_lag + (y_prev - y_next) / denom;
        }
    }

    float sin_theta = refined_lag * SPEED_OF_SOUND / (SAMPLE_RATE * MIC_SPACING_M);
    if (sin_theta > 1.0f) sin_theta = 1.0f;
    if (sin_theta < -1.0f) sin_theta = -1.0f;

    float theta_deg = asinf(sin_theta) * 180.0f / (float)M_PI;

    last_max_val_ = max_val;
    return theta_deg;
}

void DoaTracker::PushEstimate(float angle, bool is_valid) {
    estimates_[est_idx_] = angle;
    valid_[est_idx_] = is_valid;
    est_idx_ = (est_idx_ + 1) % WINDOW_SIZE;
    if (est_count_ < WINDOW_SIZE) est_count_++;
}

float DoaTracker::GetConsistentEstimate() {
    if (est_count_ == 0) return -100.0f;
    int n = est_count_ < WINDOW_SIZE ? est_count_ : WINDOW_SIZE;
    float valid_est[WINDOW_SIZE];
    int valid_n = 0;
    for (int i = 0; i < n; i++) {
        if (valid_[i]) valid_est[valid_n++] = estimates_[i];
    }
    if (valid_n < 2) return -100.0f;

    // Check consistency: at least 60% of valid estimates should be within 20° of each other
    float sum = 0;
    for (int i = 0; i < valid_n; i++) sum += valid_est[i];
    float mean = sum / valid_n;

    int consistent = 0;
    for (int i = 0; i < valid_n; i++) {
        if (fabsf(valid_est[i] - mean) < 20.0f) consistent++;
    }
    if (consistent < valid_n * 4 / 10) return -100.0f;  // not consistent enough

    // Return weighted average of consistent estimates
    float wsum = 0;
    float wtotal = 0;
    for (int i = 0; i < valid_n; i++) {
        if (fabsf(valid_est[i] - mean) < 20.0f) {
            wsum += valid_est[i];
            wtotal += 1.0f;
        }
    }
    return wsum / wtotal;
}

void DoaTracker::UpdateServo(float angle_offset) {
    if (!servo_) return;

    // Throttle: minimum interval between servo moves
    int64_t now = esp_timer_get_time() / 1000;  // ms
    static int64_t last_servo_ms = 0;
    if (now - last_servo_ms < SERVO_MIN_INTERVAL_MS) return;

    int16_t pos_raw = servo_->ReadPosition(1);
    if (pos_raw < 0) return;
    float current_deg = (float)pos_raw / 4095.0f * 180.0f;

    float target_deg = current_deg + SMOOTH_ALPHA * angle_offset;
    if (target_deg < ANGLE_MIN) target_deg = ANGLE_MIN;
    if (target_deg > ANGLE_MAX) target_deg = ANGLE_MAX;

    if (fabsf(target_deg - current_deg) < DEAD_ZONE_DEG) return;

    last_servo_ms = now;
    current_angle_ = target_deg;
    uint16_t pos = (uint16_t)(target_deg / 180.0f * 4095);
    servo_->WritePosEx(1, pos, 500, 10);

    ESP_LOGD(TAG, "Servo: cur=%.1f off=%.1f -> %.1f", current_deg, angle_offset, target_deg);
}

void DoaTracker::ServoToCenter() {
    if (!servo_) return;
    current_angle_ = 90.0f;
    servo_->WritePosEx(1, 2047, 1000, 20);
}

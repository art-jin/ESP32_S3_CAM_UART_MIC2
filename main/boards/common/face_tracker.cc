#include "face_tracker.h"
#include "esp32_camera.h"
#include "pca9685_servo.h"
#include "human_face_detect.hpp"
#include <esp_log.h>
#include <esp_heap_caps.h>

#define TAG "FaceTracker"

FaceTracker::FaceTracker(Esp32Camera* camera, Pca9685* pca,
                         uint8_t pan_ch, uint8_t tilt_ch)
    : camera_(camera), pca_(pca), pan_ch_(pan_ch), tilt_ch_(tilt_ch) {}

FaceTracker::~FaceTracker() {
    Stop();
    if (full_buf_) {
        heap_caps_free(full_buf_);
        full_buf_ = nullptr;
    }
    if (ds_buf_) {
        heap_caps_free(ds_buf_);
        ds_buf_ = nullptr;
    }
}

bool FaceTracker::Start() {
    if (running_) return true;

    ESP_LOGI(TAG, "PSRAM free: %lu bytes", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    if (!full_buf_) {
        full_buf_ = (uint8_t*)heap_caps_malloc(640 * 480 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!ds_buf_) {
        ds_buf_ = (uint8_t*)heap_caps_malloc(DS_WIDTH * DS_HEIGHT * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!full_buf_ || !ds_buf_) {
        ESP_LOGE(TAG, "Failed to allocate frame buffers (full=%p, ds=%p)", full_buf_, ds_buf_);
        return false;
    }

    pan_angle_ = 90;
    tilt_angle_ = 90;
    lost_count_ = 0;
    running_ = true;

    BaseType_t ret = xTaskCreatePinnedToCore(
        TaskEntry, "face_track", 16384, this, 2, &task_, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        running_ = false;
        return false;
    }

    ESP_LOGI(TAG, "Face tracking started, stack=16KB, core=0, pri=2");
    return true;
}

void FaceTracker::Stop() {
    if (!running_) return;
    running_ = false;
    vTaskDelay(pdMS_TO_TICKS(200));
    if (task_) {
        vTaskDelete(task_);
        task_ = nullptr;
    }

    if (pca_) {
        pca_->SetServoAngle(pan_ch_, 90);
        pca_->SetServoAngle(tilt_ch_, 90);
    }
    ESP_LOGI(TAG, "Face tracking stopped, servos centered");
}

bool FaceTracker::IsRunning() const {
    return running_;
}

void FaceTracker::TaskEntry(void* arg) {
    static_cast<FaceTracker*>(arg)->TaskLoop();
}

void FaceTracker::TaskLoop() {
    ESP_LOGI(TAG, "Creating HumanFaceDetect detector...");
    HumanFaceDetect* detector = new HumanFaceDetect();
    if (!detector) {
        ESP_LOGE(TAG, "Failed to create detector");
        running_ = false;
        return;
    }
    ESP_LOGI(TAG, "Detector created OK, PSRAM free: %lu",
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    int frame_count = 0;
    int detect_count = 0;

    while (running_) {
        uint16_t w = 0, h = 0;
        bool captured = camera_->CaptureRawRGB565(full_buf_, 640 * 480 * 2, &w, &h);
        if (!captured) {
            if (frame_count == 0) {
                ESP_LOGW(TAG, "Camera capture failed, retrying...");
            }
            vTaskDelay(pdMS_TO_TICKS(33));
            continue;
        }

        if (frame_count == 0) {
            ESP_LOGI(TAG, "First frame captured: %dx%d", w, h);
        }

        DownsampleRGB565(full_buf_, w, h, ds_buf_, DS_WIDTH, DS_HEIGHT);

        dl::image::img_t img = {};
        img.data = ds_buf_;
        img.width = DS_WIDTH;
        img.height = DS_HEIGHT;
        img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565;

        auto& results = detector->run(img);
        frame_count++;

        if (!results.empty()) {
            const auto& face = results.front();
            int cx = (face.box[0] + face.box[2]) / 2;
            int cy = (face.box[1] + face.box[3]) / 2;
            // Camera image is horizontally mirrored vs physical scene
            cx = DS_WIDTH - 1 - cx;
            detect_count++;
            if (detect_count <= 3 || detect_count % 30 == 0) {
                ESP_LOGI(TAG, "Face #%d: score=%.2f box=[%d,%d,%d,%d] center=(%d,%d) pan=%.1f tilt=%.1f",
                         detect_count, face.score,
                         face.box[0], face.box[1], face.box[2], face.box[3],
                         cx, cy, pan_angle_, tilt_angle_);
            }
            UpdateServos(cx, cy, DS_WIDTH, DS_HEIGHT);
            lost_count_ = 0;
        } else {
            lost_count_++;
            if (lost_count_ > LOST_LIMIT) {
                ReturnToCenter();
            }
            if (frame_count % 100 == 0) {
                ESP_LOGI(TAG, "No face detected after %d frames (lost=%d)", frame_count, lost_count_);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    delete detector;
    ESP_LOGI(TAG, "Task exiting, processed %d frames, %d detections", frame_count, detect_count);
}

void FaceTracker::DownsampleRGB565(const uint8_t* src, uint16_t src_w, uint16_t src_h,
                                   uint8_t* dst, uint16_t dst_w, uint16_t dst_h) {
    uint16_t* src16 = (uint16_t*)src;
    uint16_t* dst16 = (uint16_t*)dst;
    int x_ratio = src_w / dst_w;
    int y_ratio = src_h / dst_h;

    for (int y = 0; y < dst_h; y++) {
        const uint16_t* src_row = src16 + (y * y_ratio) * src_w;
        for (int x = 0; x < dst_w; x++) {
            *dst16++ = src_row[x * x_ratio];
        }
    }
}

void FaceTracker::UpdateServos(int cx, int cy, int img_w, int img_h) {
    if (!pca_) return;

    int err_x = cx - img_w / 2;
    int err_y = cy - img_h / 2;

    if (abs(err_x) > DEAD_ZONE) {
        float delta = KP * (float)err_x / (img_w / 2) * MAX_STEP;
        pan_angle_ += SMOOTH_ALPHA * delta;
    }
    if (abs(err_y) > DEAD_ZONE) {
        float delta = KP * (float)err_y / (img_h / 2) * MAX_STEP;
        tilt_angle_ -= SMOOTH_ALPHA * delta;
    }

    if (pan_angle_ < ANGLE_MIN) pan_angle_ = ANGLE_MIN;
    if (pan_angle_ > ANGLE_MAX) pan_angle_ = ANGLE_MAX;
    if (tilt_angle_ < ANGLE_MIN) tilt_angle_ = ANGLE_MIN;
    if (tilt_angle_ > ANGLE_MAX) tilt_angle_ = ANGLE_MAX;

    pca_->SetServoAngle(pan_ch_, (int)pan_angle_);
    pca_->SetServoAngle(tilt_ch_, (int)tilt_angle_);
}

void FaceTracker::ReturnToCenter() {
    if (!pca_) return;
    bool moved = false;
    if (pan_angle_ > 91) { pan_angle_ -= 1; moved = true; }
    else if (pan_angle_ < 89) { pan_angle_ += 1; moved = true; }
    else { pan_angle_ = 90; }

    if (tilt_angle_ > 91) { tilt_angle_ -= 1; moved = true; }
    else if (tilt_angle_ < 89) { tilt_angle_ += 1; moved = true; }
    else { tilt_angle_ = 90; }

    if (moved) {
        pca_->SetServoAngle(pan_ch_, (int)pan_angle_);
        pca_->SetServoAngle(tilt_ch_, (int)tilt_angle_);
    }
}

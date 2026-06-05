#include "waveshare_emoji_display.h"

#include <esp_log.h>

#include "display/lvgl_display/emoji_collection.h"
#include "display/lvgl_display/lvgl_image.h"
#include "display/lvgl_display/lvgl_theme.h"

#define TAG "WaveshareEmoji"

// Embedded GIF data (EMBED_FILES in main CMakeLists.txt — symbol names use filename only)
extern const char staticstate_gif_start[] asm("_binary_staticstate_gif_start");
extern const char staticstate_gif_end[] asm("_binary_staticstate_gif_end");
extern const char happy_gif_start[] asm("_binary_happy_gif_start");
extern const char happy_gif_end[] asm("_binary_happy_gif_end");
extern const char sad_gif_start[] asm("_binary_sad_gif_start");
extern const char sad_gif_end[] asm("_binary_sad_gif_end");
extern const char anger_gif_start[] asm("_binary_anger_gif_start");
extern const char anger_gif_end[] asm("_binary_anger_gif_end");
extern const char scare_gif_start[] asm("_binary_scare_gif_start");
extern const char scare_gif_end[] asm("_binary_scare_gif_end");
extern const char buxue_gif_start[] asm("_binary_buxue_gif_start");
extern const char buxue_gif_end[] asm("_binary_buxue_gif_end");

static void* gif_data(const char* start, const char* end) {
    return (void*)start;
}

static size_t gif_size(const char* start, const char* end) {
    return end - start;
}

WaveshareEmojiDisplay::WaveshareEmojiDisplay(
    esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
    int width, int height, int offset_x, int offset_y,
    bool mirror_x, bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y,
                    mirror_x, mirror_y, swap_xy) {
    InitializeEmojis();

    // Emoji area: top half of display (240px height -> emoji 120px, leave bottom for DOA bar)
    DisplayLockGuard lock(this);
    lv_obj_set_size(preview_image_, width_, height_ / 2);
    lv_obj_align(preview_image_, LV_ALIGN_TOP_MID, 0, 0);

    SetTheme(LvglThemeManager::GetInstance().GetTheme("dark"));
}

void WaveshareEmojiDisplay::InitializeEmojis() {
    ESP_LOGI(TAG, "Initializing GIF emoji collection");

    auto collection = std::make_shared<EmojiCollection>();

    // Neutral/calm -> staticstate
    collection->AddEmoji("staticstate", new LvglRawImage(gif_data(staticstate_gif_start, staticstate_gif_end), gif_size(staticstate_gif_start, staticstate_gif_end)));
    collection->AddEmoji("neutral", new LvglRawImage(gif_data(staticstate_gif_start, staticstate_gif_end), gif_size(staticstate_gif_start, staticstate_gif_end)));
    collection->AddEmoji("relaxed", new LvglRawImage(gif_data(staticstate_gif_start, staticstate_gif_end), gif_size(staticstate_gif_start, staticstate_gif_end)));
    collection->AddEmoji("sleepy", new LvglRawImage(gif_data(staticstate_gif_start, staticstate_gif_end), gif_size(staticstate_gif_start, staticstate_gif_end)));
    collection->AddEmoji("idle", new LvglRawImage(gif_data(staticstate_gif_start, staticstate_gif_end), gif_size(staticstate_gif_start, staticstate_gif_end)));

    // Positive/happy -> happy
    collection->AddEmoji("happy", new LvglRawImage(gif_data(happy_gif_start, happy_gif_end), gif_size(happy_gif_start, happy_gif_end)));
    collection->AddEmoji("laughing", new LvglRawImage(gif_data(happy_gif_start, happy_gif_end), gif_size(happy_gif_start, happy_gif_end)));
    collection->AddEmoji("funny", new LvglRawImage(gif_data(happy_gif_start, happy_gif_end), gif_size(happy_gif_start, happy_gif_end)));
    collection->AddEmoji("loving", new LvglRawImage(gif_data(happy_gif_start, happy_gif_end), gif_size(happy_gif_start, happy_gif_end)));
    collection->AddEmoji("confident", new LvglRawImage(gif_data(happy_gif_start, happy_gif_end), gif_size(happy_gif_start, happy_gif_end)));
    collection->AddEmoji("winking", new LvglRawImage(gif_data(happy_gif_start, happy_gif_end), gif_size(happy_gif_start, happy_gif_end)));
    collection->AddEmoji("cool", new LvglRawImage(gif_data(happy_gif_start, happy_gif_end), gif_size(happy_gif_start, happy_gif_end)));
    collection->AddEmoji("delicious", new LvglRawImage(gif_data(happy_gif_start, happy_gif_end), gif_size(happy_gif_start, happy_gif_end)));
    collection->AddEmoji("kissy", new LvglRawImage(gif_data(happy_gif_start, happy_gif_end), gif_size(happy_gif_start, happy_gif_end)));
    collection->AddEmoji("silly", new LvglRawImage(gif_data(happy_gif_start, happy_gif_end), gif_size(happy_gif_start, happy_gif_end)));

    // Sad -> sad
    collection->AddEmoji("sad", new LvglRawImage(gif_data(sad_gif_start, sad_gif_end), gif_size(sad_gif_start, sad_gif_end)));
    collection->AddEmoji("crying", new LvglRawImage(gif_data(sad_gif_start, sad_gif_end), gif_size(sad_gif_start, sad_gif_end)));

    // Angry -> anger
    collection->AddEmoji("anger", new LvglRawImage(gif_data(anger_gif_start, anger_gif_end), gif_size(anger_gif_start, anger_gif_end)));
    collection->AddEmoji("angry", new LvglRawImage(gif_data(anger_gif_start, anger_gif_end), gif_size(anger_gif_start, anger_gif_end)));

    // Surprised -> scare
    collection->AddEmoji("scare", new LvglRawImage(gif_data(scare_gif_start, scare_gif_end), gif_size(scare_gif_start, scare_gif_end)));
    collection->AddEmoji("surprised", new LvglRawImage(gif_data(scare_gif_start, scare_gif_end), gif_size(scare_gif_start, scare_gif_end)));
    collection->AddEmoji("shocked", new LvglRawImage(gif_data(scare_gif_start, scare_gif_end), gif_size(scare_gif_start, scare_gif_end)));

    // Thinking/confused -> buxue
    collection->AddEmoji("buxue", new LvglRawImage(gif_data(buxue_gif_start, buxue_gif_end), gif_size(buxue_gif_start, buxue_gif_end)));
    collection->AddEmoji("thinking", new LvglRawImage(gif_data(buxue_gif_start, buxue_gif_end), gif_size(buxue_gif_start, buxue_gif_end)));
    collection->AddEmoji("confused", new LvglRawImage(gif_data(buxue_gif_start, buxue_gif_end), gif_size(buxue_gif_start, buxue_gif_end)));
    collection->AddEmoji("embarrassed", new LvglRawImage(gif_data(buxue_gif_start, buxue_gif_end), gif_size(buxue_gif_start, buxue_gif_end)));

    auto& theme_manager = LvglThemeManager::GetInstance();
    auto light_theme = theme_manager.GetTheme("light");
    auto dark_theme = theme_manager.GetTheme("dark");

    if (light_theme != nullptr) {
        light_theme->set_emoji_collection(collection);
    }
    if (dark_theme != nullptr) {
        dark_theme->set_emoji_collection(collection);
    }

    SetEmotion("staticstate");
    ESP_LOGI(TAG, "GIF emoji collection initialized (6 animated GIFs)");
}

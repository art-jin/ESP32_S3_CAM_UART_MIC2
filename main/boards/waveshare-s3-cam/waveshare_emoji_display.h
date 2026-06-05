#pragma once

#include "display/lcd_display.h"

class WaveshareEmojiDisplay : public SpiLcdDisplay {
public:
    WaveshareEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                          int width, int height, int offset_x, int offset_y,
                          bool mirror_x, bool mirror_y, bool swap_xy);
    virtual ~WaveshareEmojiDisplay() = default;

private:
    void InitializeEmojis();
};

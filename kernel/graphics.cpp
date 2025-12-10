#include "graphics.h"
#include "font.h"

static struct limine_framebuffer* framebuffer = nullptr;

void gfx_init(struct limine_framebuffer* fb) {
    framebuffer = fb;
}

void gfx_put_pixel(int32_t x, int32_t y, uint32_t color) {
    if (!framebuffer) return;
    if (x < 0 || y < 0) return;
    if (x >= (int32_t)framebuffer->width || y >= (int32_t)framebuffer->height) return;
    
    uint32_t* fb_ptr = (uint32_t*)framebuffer->address;
    fb_ptr[y * (framebuffer->pitch / 4) + x] = color;
}

void gfx_clear(uint32_t color) {
    if (!framebuffer) return;
    
    uint32_t* fb_ptr = (uint32_t*)framebuffer->address;
    for (uint64_t y = 0; y < framebuffer->height; y++) {
        for (uint64_t x = 0; x < framebuffer->width; x++) {
            fb_ptr[y * (framebuffer->pitch / 4) + x] = color;
        }
    }
}

void gfx_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    for (int32_t py = y; py < y + h; py++) {
        for (int32_t px = x; px < x + w; px++) {
            gfx_put_pixel(px, py, color);
        }
    }
}

void gfx_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    // Top and bottom
    for (int32_t px = x; px < x + w; px++) {
        gfx_put_pixel(px, y, color);
        gfx_put_pixel(px, y + h - 1, color);
    }
    // Left and right
    for (int32_t py = y; py < y + h; py++) {
        gfx_put_pixel(x, py, color);
        gfx_put_pixel(x + w - 1, py, color);
    }
}

// Simple arrow cursor (12x19)
static const uint8_t cursor_data[] = {
    0b10000000, 0b00000000,
    0b11000000, 0b00000000,
    0b11100000, 0b00000000,
    0b11110000, 0b00000000,
    0b11111000, 0b00000000,
    0b11111100, 0b00000000,
    0b11111110, 0b00000000,
    0b11111111, 0b00000000,
    0b11111111, 0b10000000,
    0b11111111, 0b11000000,
    0b11111100, 0b00000000,
    0b11101100, 0b00000000,
    0b11000110, 0b00000000,
    0b10000110, 0b00000000,
    0b00000011, 0b00000000,
    0b00000011, 0b00000000,
    0b00000001, 0b10000000,
    0b00000001, 0b10000000,
    0b00000000, 0b00000000,
};

void gfx_draw_cursor(int32_t x, int32_t y) {
    for (int row = 0; row < 19; row++) {
        uint16_t bits = (cursor_data[row * 2] << 8) | cursor_data[row * 2 + 1];
        for (int col = 0; col < 12; col++) {
            if (bits & (0x8000 >> col)) {
                gfx_put_pixel(x + col, y + row, COLOR_WHITE);
            }
        }
    }
}

void gfx_draw_char(int32_t x, int32_t y, char c, uint32_t color) {
    if (c < 0 || c > 127) return;
    const uint8_t *glyph = font8x8[(int)c];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if ((glyph[row] >> (7 - col)) & 1) {
                gfx_put_pixel(x + col, y + row, color);
            }
        }
    }
}

void gfx_clear_char(int32_t x, int32_t y, uint32_t bg_color) {
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 9; col++) {
            gfx_put_pixel(x + col, y + row, bg_color);
        }
    }
}

void gfx_draw_string(int32_t x, int32_t y, const char *str, uint32_t color) {
    int32_t cursor_x = x;
    int32_t cursor_y = y;
    while (*str) {
        if (*str == '\n') {
            cursor_x = x;
            cursor_y += 10;
        } else {
            gfx_draw_char(cursor_x, cursor_y, *str, color);
            cursor_x += 9;
        }
        str++;
    }
}

void gfx_draw_centered_text(const char* text, uint32_t color) {
    if (!framebuffer) return;
    
    int text_len = 0;
    const char* p = text;
    while (*p++) text_len++;
    
    int char_width = 8;
    int text_width = text_len * char_width;
    int center_x = (framebuffer->width - text_width) / 2;
    int center_y = (framebuffer->height - 16) / 2;
    
    gfx_draw_string(center_x, center_y, text, color);
}

void gfx_scroll_up(int pixels, uint32_t fill_color) {
    if (!framebuffer || pixels <= 0) return;
    
    uint32_t* fb = (uint32_t*)framebuffer->address;
    uint64_t pitch = framebuffer->pitch / 4;  // pitch in uint32_t units
    uint64_t width = framebuffer->width;
    uint64_t height = framebuffer->height;
    
    // Clamp pixels to height
    if ((uint64_t)pixels >= height) {
        gfx_clear(fill_color);
        return;
    }
    
    uint64_t rows_to_move = height - pixels;
    
    // Optimized bulk memory move using 64-bit operations
    // Copy entire scroll region at once for better cache performance
    uint64_t* dst64 = (uint64_t*)fb;
    uint64_t* src64 = (uint64_t*)(fb + pixels * pitch);
    uint64_t count64 = (rows_to_move * pitch) / 2;  // 2 pixels per uint64_t
    
    for (uint64_t i = 0; i < count64; i++) {
        dst64[i] = src64[i];
    }
    
    // Fill bottom rows with fill_color using 64-bit fill
    uint64_t fill64 = ((uint64_t)fill_color << 32) | fill_color;
    for (uint64_t y = rows_to_move; y < height; y++) {
        uint64_t* row64 = (uint64_t*)(fb + y * pitch);
        uint64_t count = width / 2;
        for (uint64_t x = 0; x < count; x++) {
            row64[x] = fill64;
        }
        // Handle odd width
        if (width & 1) {
            fb[y * pitch + width - 1] = fill_color;
        }
    }
}

uint64_t gfx_get_width() {
    return framebuffer ? framebuffer->width : 0;
}

uint64_t gfx_get_height() {
    return framebuffer ? framebuffer->height : 0;
}

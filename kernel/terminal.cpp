#include "terminal.h"
#include "graphics.h"
#include "timer.h"

Terminal g_terminal;

static const int CHAR_WIDTH = 9;
static const int CHAR_HEIGHT = 10;
static const int MARGIN_LEFT = 50;
static const int MARGIN_TOP = 50;
static const int MARGIN_BOTTOM = 30;

Terminal::Terminal() 
    : cursor_col(0), cursor_row(0), 
      fg_color(COLOR_WHITE), bg_color(COLOR_BLACK),
      cursor_visible(true), cursor_state(true), last_blink_tick(0) {
}

void Terminal::init(uint32_t fg, uint32_t bg) {
    fg_color = fg;
    bg_color = bg;
    
    uint64_t screen_w = gfx_get_width();
    uint64_t screen_h = gfx_get_height();
    
    if (screen_w == 0 || screen_h == 0) return;
    
    width_chars = (screen_w - MARGIN_LEFT * 2) / CHAR_WIDTH;
    height_chars = (screen_h - MARGIN_TOP - MARGIN_BOTTOM) / CHAR_HEIGHT;
    
    clear();
}

void Terminal::clear() {
    gfx_clear(bg_color);
    cursor_col = 0;
    cursor_row = 0;
    
    // Draw header if needed, or just leave blank
    // gfx_draw_string(MARGIN_LEFT, MARGIN_TOP - 20, "uniOS Terminal", fg_color);
}

void Terminal::set_color(uint32_t fg, uint32_t bg) {
    fg_color = fg;
    bg_color = bg;
}

void Terminal::set_cursor_pos(int col, int row) {
    // Erase old cursor
    draw_cursor(false);
    
    cursor_col = col;
    cursor_row = row;
    
    if (cursor_col < 0) cursor_col = 0;
    if (cursor_col >= width_chars) cursor_col = width_chars - 1;
    
    if (cursor_row < 0) cursor_row = 0;
    if (cursor_row >= height_chars) cursor_row = height_chars - 1;
    
    // Draw new cursor
    draw_cursor(true);
}

void Terminal::get_cursor_pos(int* col, int* row) {
    if (col) *col = cursor_col;
    if (row) *row = cursor_row;
}

void Terminal::put_char(char c) {
    // Hide cursor before drawing
    draw_cursor(false);
    
    if (c == '\n') {
        new_line();
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            int x = MARGIN_LEFT + cursor_col * CHAR_WIDTH;
            int y = MARGIN_TOP + cursor_row * CHAR_HEIGHT;
            gfx_clear_char(x, y, bg_color);
        }
    } else if (c >= 32) {
        int x = MARGIN_LEFT + cursor_col * CHAR_WIDTH;
        int y = MARGIN_TOP + cursor_row * CHAR_HEIGHT;
        
        gfx_draw_char(x, y, c, fg_color);
        
        cursor_col++;
        if (cursor_col >= width_chars) {
            new_line();
        }
    }
    
    // Show cursor after drawing
    draw_cursor(true);
    
    // Reset blink timer so cursor stays visible while typing
    cursor_state = true;
    last_blink_tick = timer_get_ticks();
}

void Terminal::write(const char* str) {
    while (*str) {
        put_char(*str++);
    }
}

void Terminal::write_line(const char* str) {
    write(str);
    put_char('\n');
}

void Terminal::new_line() {
    cursor_col = 0;
    cursor_row++;
    
    if (cursor_row >= height_chars) {
        scroll_up();
        cursor_row = height_chars - 1;
    }
}

void Terminal::scroll_up() {
    // Scroll the entire screen area used by terminal
    // We scroll by one line height
    gfx_scroll_up(CHAR_HEIGHT, bg_color);
}

void Terminal::draw_cursor(bool visible) {
    if (!cursor_visible) return;
    
    int x = MARGIN_LEFT + cursor_col * CHAR_WIDTH;
    int y = MARGIN_TOP + cursor_row * CHAR_HEIGHT;
    
    if (visible) {
        gfx_draw_char(x, y, '_', fg_color);
    } else {
        // Clear cursor position (but don't erase character if we are blinking on top of one? 
        // Standard terminals usually are block or underline. 
        // If we are just an append-only terminal, we are usually at an empty spot.
        // But if we move cursor back, we might be on a char.
        // For now, assume we are at end of line or we just clear the underline.
        // To be safe, we should redraw the character under the cursor if there is one.
        // But we don't store the buffer!
        // So we can only clear. This means 'backspace' logic in shell needs to handle redrawing.
        // For the blinking cursor at the END of text, clearing is fine.
        gfx_clear_char(x, y, bg_color); 
    }
}

void Terminal::set_cursor_visible(bool visible) {
    cursor_visible = visible;
    if (visible) {
        cursor_state = true;
        draw_cursor(true);
    } else {
        draw_cursor(false);
    }
}

void Terminal::update_cursor() {
    if (!cursor_visible) return;
    
    uint64_t now = timer_get_ticks();
    if (now - last_blink_tick > 50) { // ~500ms
        last_blink_tick = now;
        cursor_state = !cursor_state;
        draw_cursor(cursor_state);
    }
}

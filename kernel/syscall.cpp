#include "syscall.h"
#include "limine.h"
#include <stddef.h>

extern struct limine_framebuffer* g_framebuffer;
extern void draw_char(struct limine_framebuffer *fb, uint64_t x, uint64_t y, char c, uint32_t color);

static uint64_t sys_cursor_x = 50;
static uint64_t sys_cursor_y = 480;

extern "C" uint64_t syscall_handler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    (void)arg3;
    
    switch (syscall_num) {
        case 1: { // SYS_WRITE
            const char* str = (const char*)arg1;
            uint64_t len = arg2;
            
            for (uint64_t i = 0; i < len && str[i]; i++) {
                if (str[i] == '\n') {
                    sys_cursor_x = 50;
                    sys_cursor_y += 10;
                } else {
                    draw_char(g_framebuffer, sys_cursor_x, sys_cursor_y, str[i], 0x00FF00);
                    sys_cursor_x += 9;
                }
            }
            return len;
        }
        case 60: // SYS_EXIT
            // For now, just halt
            asm("cli; hlt");
            return 0;
        default:
            return (uint64_t)-1;
    }
}

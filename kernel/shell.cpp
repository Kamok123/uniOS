#include "shell.h"
#include "unifs.h"
#include "pmm.h"
#include "graphics.h"
#include "io.h"
#include <stddef.h>

static char cmd_buffer[256];
static int cmd_len = 0;
static uint64_t cursor_x = 68;
static uint64_t cursor_y = 90;  // Match kernel's initial prompt position

extern "C" void jump_to_user_mode(uint64_t code_sel, uint64_t stack, uint64_t entry);

static int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static int strncmp(const char* s1, const char* s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static void new_line() {
    cursor_x = 50;
    cursor_y += 10;
    
    if (cursor_y >= gfx_get_height() - 20) {
        gfx_scroll_up(10, COLOR_BLACK);
        cursor_y -= 10;
    }
    
    gfx_draw_string(cursor_x, cursor_y, "> ", COLOR_CYAN);
    cursor_x = 68;
}

static void clear_screen() {
    gfx_clear(COLOR_BLACK);
    cursor_x = 50;
    cursor_y = 50;
    gfx_draw_string(cursor_x, cursor_y, "uniOS Shell (uniSH)", COLOR_WHITE);
    cursor_y = 70;
    new_line();
}

static void print(const char* str) {
    while (*str) {
        if (*str == '\n') {
            cursor_x = 50;
            cursor_y += 10;
            if (cursor_y >= gfx_get_height() - 20) {
                gfx_scroll_up(10, COLOR_BLACK);
                cursor_y -= 10;
            }
        } else {
            gfx_draw_char(cursor_x, cursor_y, *str, COLOR_WHITE);
            cursor_x += 9;
            if (cursor_x >= gfx_get_width() - 50) {
                cursor_x = 50;
                cursor_y += 10;
                if (cursor_y >= gfx_get_height() - 20) {
                    gfx_scroll_up(10, COLOR_BLACK);
                    cursor_y -= 10;
                }
            }
        }
        str++;
    }
}

static void cmd_help() {
    print("Commands:\n");
    print("  help      - Show this help\n");
    print("  ls        - List files\n");
    print("  cat <f>   - Show file contents\n");
    print("  mem       - Show memory usage\n");
    print("  clear     - Clear screen\n");
    print("  gui       - Start GUI mode\n");
    print("  reboot    - Reboot system\n");
    print("  poweroff  - Shutdown system\n");
}

static void cmd_ls() {
    extern uint64_t unifs_get_file_count();
    extern const char* unifs_get_file_name(uint64_t index);
    
    uint64_t count = unifs_get_file_count();
    for (uint64_t i = 0; i < count; i++) {
        const char* name = unifs_get_file_name(i);
        if (name) {
            print(name);
            print("  ");
        }
    }
    print("\n");
}

static void cmd_cat(const char* filename) {
    const UniFSFile* file = unifs_open(filename);
    if (file) {
        for (uint64_t i = 0; i < file->size; i++) {
            char c = file->data[i];
            if (c == '\n' || c == '\r') {
                cursor_x = 50;
                cursor_y += 10;
                if (cursor_y >= gfx_get_height() - 20) {
                    gfx_scroll_up(10, COLOR_BLACK);
                    cursor_y -= 10;
                }
            } else {
                gfx_draw_char(cursor_x, cursor_y, c, COLOR_WHITE);
                cursor_x += 9;
                if (cursor_x >= gfx_get_width() - 50) {
                    cursor_x = 50;
                    cursor_y += 10;
                    if (cursor_y >= gfx_get_height() - 20) {
                        gfx_scroll_up(10, COLOR_BLACK);
                        cursor_y -= 10;
                    }
                }
            }
        }
        print("\n");
    } else {
        print("File not found.\n");
    }
}

static void cmd_mem() {
    uint64_t free_mem = pmm_get_free_memory() / 1024 / 1024;
    uint64_t total_mem = pmm_get_total_memory() / 1024 / 1024;
    
    char buf[64];
    int i = 0;
    
    buf[i++] = 'M'; buf[i++] = 'e'; buf[i++] = 'm'; buf[i++] = ':'; buf[i++] = ' ';
    uint64_t n = free_mem;
    if (n == 0) buf[i++] = '0';
    else {
        char tmp[20]; int j = 0;
        while (n > 0) { tmp[j++] = '0' + (n % 10); n /= 10; }
        while (j > 0) buf[i++] = tmp[--j];
    }
    buf[i++] = '/';
    n = total_mem;
    if (n == 0) buf[i++] = '0';
    else {
        char tmp[20]; int j = 0;
        while (n > 0) { tmp[j++] = '0' + (n % 10); n /= 10; }
        while (j > 0) buf[i++] = tmp[--j];
    }
    buf[i++] = 'M'; buf[i++] = 'B'; buf[i++] = '\n'; buf[i] = 0;
    print(buf);
}

static void execute_command() {
    cmd_buffer[cmd_len] = 0;
    
    if (cmd_len == 0) {
        new_line();
        return;
    }
    
    cursor_y += 10;
    cursor_x = 50;
    
    // Check if command execution pushes us off screen
    if (cursor_y >= gfx_get_height() - 20) {
        gfx_scroll_up(10, COLOR_BLACK);
        cursor_y -= 10;
    }
    
    if (strcmp(cmd_buffer, "help") == 0) {
        cmd_help();
    } else if (strcmp(cmd_buffer, "ls") == 0) {
        cmd_ls();
    } else if (strncmp(cmd_buffer, "cat ", 4) == 0) {
        cmd_cat(cmd_buffer + 4);
    } else if (strcmp(cmd_buffer, "mem") == 0) {
        cmd_mem();
    } else if (strcmp(cmd_buffer, "clear") == 0) {
        clear_screen();
        cmd_len = 0;
        return;
    } else if (strcmp(cmd_buffer, "gui") == 0) {
        extern void gui_start();
        gui_start();
        cursor_y = 90;  // Reset after GUI
    } else if (strncmp(cmd_buffer, "exec ", 5) == 0) {
        const char* filename = cmd_buffer + 5;
        const UniFSFile* file = unifs_open(filename);
        if (file) {
            extern bool elf_validate(const uint8_t* data, uint64_t size);
            extern uint64_t elf_load(const uint8_t* data, uint64_t size);
            if (elf_validate(file->data, file->size)) {
                uint64_t entry = elf_load(file->data, file->size);
                if (entry) {
                    void (*entry_fn)() = (void(*)())entry;
                    entry_fn();
                }
            }
        } else {
            print("File not found.\n");
        }
    } else if (strncmp(cmd_buffer, "run3 ", 5) == 0) {
        const char* filename = cmd_buffer + 5;
        const UniFSFile* file = unifs_open(filename);
        if (file) {
            extern bool elf_validate(const uint8_t* data, uint64_t size);
            extern uint64_t elf_load_user(const uint8_t* data, uint64_t size);
            if (elf_validate(file->data, file->size)) {
                uint64_t entry = elf_load_user(file->data, file->size);
                if (entry) {
                    jump_to_user_mode(0x1B, 0x7FFF1000, entry);
                }
            }
        } else {
            print("File not found.\n");
        }
    } else if (strcmp(cmd_buffer, "reboot") == 0) {
        print("Rebooting...\n");
        // Wait for keyboard controller to be ready
        while (inb(0x64) & 0x02) {}
        // Send reset command via PS/2 controller
        outb(0x64, 0xFE);
        // If that didn't work, halt
        while (1) { asm("hlt"); }
    } else if (strcmp(cmd_buffer, "poweroff") == 0 || strcmp(cmd_buffer, "shutdown") == 0) {
        print("Shutting down...\n");
        // Try QEMU/Bochs shutdown
        outw(0x604, 0x2000);
        // Try older QEMU
        outw(0xB004, 0x2000);
        // Try VirtualBox
        outw(0x4004, 0x3400);
        // If none worked, halt
        print("Shutdown failed. Halting.\n");
        while (1) { asm("hlt"); }
    } else {
        print("Unknown command.\n");
    }
    
    cmd_len = 0;
    new_line();
}

void shell_init(struct limine_framebuffer* fb) {
    // framebuffer = fb; // No longer needed, using gfx module
    cmd_len = 0;
    cursor_x = 68;
    cursor_y = 90;  // Set initial position
}

void shell_process_char(char c) {
    if (c == '\n') {
        execute_command();
    } else if (c == '\b') {
        if (cmd_len > 0) {
            cmd_len--;
            cursor_x -= 9;
            gfx_clear_char(cursor_x, cursor_y, COLOR_BLACK);
        }
    } else if (cmd_len < 255) {
        cmd_buffer[cmd_len++] = c;
        gfx_draw_char(cursor_x, cursor_y, c, COLOR_WHITE);
        cursor_x += 9;
        
        // Wrap text while typing
        if (cursor_x >= gfx_get_width() - 50) {
             cursor_x = 50;
             cursor_y += 10;
             if (cursor_y >= gfx_get_height() - 20) {
                 gfx_scroll_up(10, COLOR_BLACK);
                 cursor_y -= 10;
             }
        }
    }
}

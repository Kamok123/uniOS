#include "shell.h"
#include "terminal.h"
#include "graphics.h"
#include "unifs.h"
#include "pmm.h"
#include "io.h"
#include "acpi.h"
#include "timer.h"
#include "input.h"
#include <stddef.h>

static char cmd_buffer[256];
static int cmd_len = 0;
static int cursor_pos = 0; // Position within cmd_buffer

// Special key codes
#define KEY_RIGHT 1
#define KEY_LEFT  2
#define KEY_DOWN  3
#define KEY_UP    4

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

static void cmd_help() {
    g_terminal.write_line("Commands:");
    g_terminal.write_line("  help      - Show this help");
    g_terminal.write_line("  ls        - List files");
    g_terminal.write_line("  cat <f>   - Show file contents");
    g_terminal.write_line("  mem       - Show memory usage");
    g_terminal.write_line("  clear     - Clear screen");
    g_terminal.write_line("  gui       - Start GUI mode");
    g_terminal.write_line("  reboot    - Reboot system");
    g_terminal.write_line("  poweroff  - Shutdown system");
}

static void cmd_ls() {
    extern uint64_t unifs_get_file_count();
    extern const char* unifs_get_file_name(uint64_t index);
    
    uint64_t count = unifs_get_file_count();
    for (uint64_t i = 0; i < count; i++) {
        const char* name = unifs_get_file_name(i);
        if (name) {
            g_terminal.write(name);
            g_terminal.write("  ");
        }
    }
    g_terminal.write("\n");
}

static void cmd_cat(const char* filename) {
    const UniFSFile* file = unifs_open(filename);
    if (file) {
        for (uint64_t i = 0; i < file->size; i++) {
            g_terminal.put_char(file->data[i]);
        }
        g_terminal.write("\n");
    } else {
        g_terminal.write_line("File not found.");
    }
}

static void cmd_mem() {
    uint64_t free_bytes = pmm_get_free_memory();
    uint64_t total_bytes = pmm_get_total_memory();
    uint64_t used_bytes = total_bytes - free_bytes;
    
    uint64_t free_kb = free_bytes / 1024;
    uint64_t total_kb = total_bytes / 1024;
    uint64_t used_kb = used_bytes / 1024;
    
    char buf[128];
    int i = 0;
    
    auto append_str = [&](const char* s) {
        while (*s) buf[i++] = *s++;
    };
    
    auto append_num = [&](uint64_t n) {
        if (n == 0) { buf[i++] = '0'; return; }
        char tmp[20]; int j = 0;
        while (n > 0) { tmp[j++] = '0' + (n % 10); n /= 10; }
        while (j > 0) buf[i++] = tmp[--j];
    };
    
    append_str("Memory Status:\n");
    
    append_str("  Total: "); append_num(total_kb); append_str(" KB ("); 
    append_num(total_kb / 1024); append_str(" MB)\n");
    
    append_str("  Used:  "); append_num(used_kb); append_str(" KB\n");
    
    append_str("  Free:  "); append_num(free_kb); append_str(" KB\n");
    
    buf[i] = 0;
    g_terminal.write(buf);
}

static void execute_command() {
    cmd_buffer[cmd_len] = 0;
    
    if (cmd_len == 0) {
        g_terminal.write("\n> ");
        return;
    }
    
    g_terminal.write("\n"); // Move to next line after command input
    
    if (strcmp(cmd_buffer, "help") == 0) {
        cmd_help();
    } else if (strcmp(cmd_buffer, "ls") == 0) {
        cmd_ls();
    } else if (strncmp(cmd_buffer, "cat ", 4) == 0) {
        cmd_cat(cmd_buffer + 4);
    } else if (strcmp(cmd_buffer, "mem") == 0) {
        cmd_mem();
    } else if (strcmp(cmd_buffer, "clear") == 0) {
        g_terminal.clear();
        g_terminal.write("uniOS Shell (uniSH)\n\n");
        cmd_len = 0;
        cursor_pos = 0;
        g_terminal.write("> ");
        return;
    } else if (strcmp(cmd_buffer, "gui") == 0) {
        extern void gui_start();
        gui_start();
        // After GUI, clear and reset
        g_terminal.clear();
        g_terminal.write("uniOS Shell (uniSH)\n\n");
    } else if (strcmp(cmd_buffer, "reboot") == 0) {
        g_terminal.write_line("Rebooting...");
        
        // 1. Keyboard Controller Reset (0x64 = 0xFE)
        outb(0x64, 0xFE);
        for (volatile int i = 0; i < 1000000; i++);
        
        // 2. PCI Reset (0xCF9 = 0x06) - Reset + System Reset
        outb(0xCF9, 0x06);
        for (volatile int i = 0; i < 1000000; i++);
        
        // 3. Triple Fault (load invalid IDT)
        struct {
            uint16_t limit;
            uint64_t base;
        } __attribute__((packed)) invalid_idt = { 0, 0 };
        asm volatile("lidt %0; int3" :: "m"(invalid_idt));
        
        asm volatile("cli; hlt");
    } else if (strcmp(cmd_buffer, "poweroff") == 0) {
        if (acpi_is_available()) {
            g_terminal.write_line("ACPI available, attempting shutdown...");
        } else {
            g_terminal.write_line("ACPI not available.");
        }
        acpi_poweroff();
        g_terminal.write_line("Shutdown failed.");
    } else {
        g_terminal.write("Unknown command: ");
        g_terminal.write_line(cmd_buffer);
    }
    
    cmd_len = 0;
    cursor_pos = 0;
    g_terminal.write("> ");
}

void shell_init(struct limine_framebuffer* fb) {
    (void)fb; // Not used directly anymore
    g_terminal.init(COLOR_WHITE, COLOR_BLACK);
    g_terminal.write("uniOS Shell (uniSH)\n");
    g_terminal.write("Type 'help' for commands.\n\n");
    g_terminal.write("> ");
    
    cmd_len = 0;
    cursor_pos = 0;
}

void shell_process_char(char c) {
    if (c == '\n') {
        execute_command();
    } else if (c == '\b') {
        if (cursor_pos > 0) {
            // Remove char from buffer
            for (int i = cursor_pos - 1; i < cmd_len - 1; i++) {
                cmd_buffer[i] = cmd_buffer[i + 1];
            }
            cmd_len--;
            cursor_pos--;
            
            // Update screen: backspace, then print remainder, then space, then back up
            // This is tricky with just a dumb terminal.
            // Easiest is to reprint the line.
            // But Terminal doesn't support "move cursor back N chars" easily yet except via direct set_pos.
            // Let's just use backspace on terminal which clears the char.
            // But we need to handle insertion/deletion in middle.
            
            // Simple approach: Backspace visual only works for end of line deletion in dumb terminal
            // For proper editing, we need to redraw the line.
            // Let's implement simple end-of-line backspace for now.
            g_terminal.put_char('\b');
        }
    } else if (c >= 32 && cmd_len < 255) {
        cmd_buffer[cmd_len++] = c;
        cursor_pos++;
        g_terminal.put_char(c);
    }
}

void shell_tick() {
    g_terminal.update_cursor();
}

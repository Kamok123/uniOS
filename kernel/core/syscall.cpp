#include "syscall.h"
#include "limine.h"
#include "unifs.h"
#include "pipe.h"
#include "process.h"
#include "debug.h"
#include "graphics.h"
#include <stddef.h>

// ============================================================================
// User Pointer Validation
// ============================================================================
// User space addresses are in the lower half of the address space (< 0x0000800000000000)
// Kernel space (HHDM) starts at the higher half (>= 0xFFFF800000000000)

#define USER_SPACE_MAX 0x0000800000000000ULL

/**
 * @brief Validate that a user pointer is actually in user space
 * @param ptr Pointer to validate
 * @param size Size of the memory region (for overflow checking)
 * @return true if pointer is valid user space address, false otherwise
 */
static bool validate_user_ptr(const void* ptr, size_t size) {
    uint64_t addr = (uint64_t)ptr;
    
    // Null pointer check
    if (addr == 0) return false;
    
    // User space check - must be in lower half
    if (addr >= USER_SPACE_MAX) return false;
    
    // Overflow check - end of region must not wrap or enter kernel space
    if (size > 0) {
        uint64_t end = addr + size - 1;
        if (end < addr) return false;  // Overflow
        if (end >= USER_SPACE_MAX) return false;
    }
    
    return true;
}

/**
 * @brief Validate a user string pointer (null-terminated)
 * @param str String pointer to validate
 * @param max_len Maximum length to check
 * @return Length of string, or (size_t)-1 if invalid
 */
static size_t validate_user_string(const char* str, size_t max_len) {
    if (!validate_user_ptr(str, 1)) return (size_t)-1;
    
    // Walk the string, checking each byte
    for (size_t i = 0; i < max_len; i++) {
        if (!validate_user_ptr(str + i, 1)) return (size_t)-1;
        if (str[i] == '\0') return i;
    }
    
    // String too long
    return (size_t)-1;
}

static uint64_t sys_cursor_x = 50;
static uint64_t sys_cursor_y = 480;

// File descriptor table (simple, single-process for now)
static FileDescriptor fd_table[MAX_OPEN_FILES];
static bool fd_initialized = false;

static void init_fd_table() {
    if (fd_initialized) return;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        fd_table[i].in_use = false;
    }
    // Reserve stdin/stdout/stderr
    fd_table[0].in_use = true; // stdin
    fd_table[1].in_use = true; // stdout
    fd_table[2].in_use = true; // stderr
    fd_initialized = true;
}

static int find_free_fd() {
    for (int i = 3; i < MAX_OPEN_FILES; i++) {
        if (!fd_table[i].in_use) return i;
    }
    return -1;
}

// Check if a file is currently open in fd_table
// Used by filesystem to prevent deletion of open files
bool is_file_open(const char* filename) {
    if (!filename || !fd_initialized) return false;
    
    for (int i = 3; i < MAX_OPEN_FILES; i++) {
        if (fd_table[i].in_use && fd_table[i].filename) {
            // Compare filenames
            const char* a = fd_table[i].filename;
            const char* b = filename;
            bool match = true;
            while (*a && *b) {
                if (*a++ != *b++) { match = false; break; }
            }
            if (match && *a == *b) return true;
        }
    }
    return false;
}

// SYS_OPEN: open(filename, flags, mode) -> fd
static uint64_t sys_open(const char* filename) {
    // Validate user pointer
    if (validate_user_string(filename, 4096) == (size_t)-1) {
        return (uint64_t)-1;
    }
    
    init_fd_table();
    
    // Use thread-safe version with local buffer to avoid race condition
    UniFSFile file;
    if (!unifs_open_into(filename, &file)) {
        return (uint64_t)-1;
    }
    
    int fd = find_free_fd();
    if (fd < 0) return (uint64_t)-1;
    
    // Copy data from local buffer to fd_table (safe, won't be overwritten)
    fd_table[fd].in_use = true;
    fd_table[fd].filename = file.name;
    fd_table[fd].position = 0;
    fd_table[fd].size = file.size;
    fd_table[fd].data = file.data;
    
    return fd;
}

// SYS_READ: read(fd, buf, count) -> bytes_read
static uint64_t sys_read(int fd, char* buf, uint64_t count) {
    // Validate user buffer
    if (count > 0 && !validate_user_ptr(buf, count)) {
        return (uint64_t)-1;
    }
    
    init_fd_table();
    
    if (fd < 0 || fd >= MAX_OPEN_FILES || !fd_table[fd].in_use) {
        return (uint64_t)-1;
    }
    
    if (fd == STDIN_FD) {
        // TODO: Read from keyboard
        return 0;
    }
    
    FileDescriptor* f = &fd_table[fd];
    uint64_t remaining = f->size - f->position;
    uint64_t to_read = (count < remaining) ? count : remaining;
    
    for (uint64_t i = 0; i < to_read; i++) {
        buf[i] = f->data[f->position + i];
    }
    f->position += to_read;
    
    return to_read;
}

// SYS_WRITE: write(fd, buf, count) -> bytes_written
static uint64_t sys_write(int fd, const char* buf, uint64_t count) {
    // Validate user buffer
    if (count > 0 && !validate_user_ptr(buf, count)) {
        return (uint64_t)-1;
    }
    
    if (fd == STDOUT_FD || fd == STDERR_FD) {
        for (uint64_t i = 0; i < count && buf[i]; i++) {
            if (buf[i] == '\n') {
                sys_cursor_x = 50;
                sys_cursor_y += 10;
            } else {
                gfx_draw_char(sys_cursor_x, sys_cursor_y, buf[i], COLOR_GREEN);
                sys_cursor_x += 9;
            }
        }
        return count;
    }
    return (uint64_t)-1; // Can't write to files (read-only FS)
}

// SYS_CLOSE: close(fd) -> 0 on success
static uint64_t sys_close(int fd) {
    init_fd_table();
    
    if (fd < 3 || fd >= MAX_OPEN_FILES) return (uint64_t)-1;
    if (!fd_table[fd].in_use) return (uint64_t)-1;
    
    fd_table[fd].in_use = false;
    return 0;
}

// Process ID (simple, single PID for now)
static uint64_t current_pid = 1;

extern "C" uint64_t syscall_handler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    // DEBUG_LOG("Syscall: %d\n", syscall_num); // Uncomment for verbose logging
    
    switch (syscall_num) {
        case SYS_READ:
            return sys_read((int)arg1, (char*)arg2, arg3);
        case SYS_WRITE:
            return sys_write((int)arg1, (const char*)arg2, arg3);
        case SYS_OPEN:
            return sys_open((const char*)arg1);
        case SYS_CLOSE:
            return sys_close((int)arg1);
        case SYS_PIPE:
            return pipe_create();
        case SYS_GETPID: {
            extern Process* process_get_current();
            Process* p = process_get_current();
            return p ? p->pid : 1;
        }
        case SYS_FORK: {
            extern uint64_t process_fork();
            return process_fork();
        }
        case SYS_EXIT: {
            extern void process_exit(int32_t status);
            process_exit((int32_t)arg1);
            return 0;
        }
        case SYS_WAIT4: {
            // Validate status pointer if provided
            if (arg2 != 0 && !validate_user_ptr((void*)arg2, sizeof(int32_t))) {
                return (uint64_t)-1;
            }
            extern int64_t process_waitpid(int64_t pid, int32_t* status);
            return process_waitpid((int64_t)arg1, (int32_t*)arg2);
        }
        default:
            DEBUG_WARN("Unknown syscall: %d\n", syscall_num);
            return (uint64_t)-1;
    }
}

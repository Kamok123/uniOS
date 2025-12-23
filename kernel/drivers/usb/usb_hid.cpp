#include "usb_hid.h"
#include "usb.h"
#include "xhci.h"
#include "io.h"
#include "timer.h"
#include <stddef.h>

// Keyboard state
static bool keyboard_available = false;
static UsbDeviceInfo* keyboard_device = nullptr;
static HidKeyboardReport last_keyboard_report = {0};

// Keyboard buffer
#define KB_BUFFER_SIZE 256
static char kb_buffer[KB_BUFFER_SIZE];
static volatile uint8_t kb_buffer_start = 0;
static volatile uint8_t kb_buffer_end = 0;

// Mouse state
static bool mouse_available = false;
static bool mouse_data_received = false;  // True only if USB mouse has sent data
static UsbDeviceInfo* mouse_device = nullptr;
static int32_t mouse_x = 0;
static int32_t mouse_y = 0;
static bool mouse_left = false;
static bool mouse_right = false;
static bool mouse_middle = false;
static int8_t mouse_scroll = 0;  // Scroll wheel delta accumulator

// Screen dimensions (set during init)
static int32_t screen_width = 1024;
static int32_t screen_height = 768;

// Polling interval tracking - per USB spec, must wait for interval before polling again
static uint64_t last_keyboard_poll = 0;
static uint64_t last_mouse_poll = 0;

// Debug flag - controls verbose logging
static bool hid_debug = false;

// HID keycode to ASCII conversion table (US keyboard layout)
// Index = HID keycode, value = ASCII (lowercase)
// Arrow keys use special codes matching PS/2 keyboard: 0x80=Up, 0x81=Down, 0x82=Left, 0x83=Right
#define KEY_UP_ARROW    0x80
#define KEY_DOWN_ARROW  0x81
#define KEY_LEFT_ARROW  0x82
#define KEY_RIGHT_ARROW 0x83
#define KEY_HOME        0x84
#define KEY_END         0x85
#define KEY_DELETE      0x86
// Shift+Arrow for text selection
#define KEY_SHIFT_LEFT  0x90
#define KEY_SHIFT_RIGHT 0x91

static const char hid_to_ascii[128] = {
    0,    0,    0,    0,   'a',  'b',  'c',  'd',   // 0x00-0x07
    'e',  'f',  'g',  'h',  'i',  'j',  'k',  'l',   // 0x08-0x0F
    'm',  'n',  'o',  'p',  'q',  'r',  's',  't',   // 0x10-0x17
    'u',  'v',  'w',  'x',  'y',  'z',  '1',  '2',   // 0x18-0x1F
    '3',  '4',  '5',  '6',  '7',  '8',  '9',  '0',   // 0x20-0x27
    '\n', 27,   '\b', '\t', ' ',  '-',  '=',  '[',   // 0x28-0x2F (Enter, Esc, Backspace, Tab, Space)
    ']',  '\\', '#',  ';',  '\'', '`',  ',',  '.',   // 0x30-0x37
    '/',  0,    0,    0,    0,    0,    0,    0,     // 0x38-0x3F (CapsLock, F1-F6)
    0,    0,    0,    0,    0,    0,    0,    0,     // 0x40-0x47 (F7-F12, PrintScreen, ScrollLock)
    0,    0,    (char)0x84, 0, (char)0x86, (char)0x85, 0, (char)0x83,  // 0x48-0x4F (Pause, Insert, Home, PageUp, Delete, End, PageDown, Right)
    (char)0x82, (char)0x81, (char)0x80, 0, '/',  '*',  '-',  '+',   // 0x50-0x57 (Left, Down, Up, NumLock, Keypad...)
    '\n', '1',  '2',  '3',  '4',  '5',  '6',  '7',   // 0x58-0x5F (Keypad Enter, 1-7)
    '8',  '9',  '0',  '.',  0,    0,    0,    '=',   // 0x60-0x67
    0,    0,    0,    0,    0,    0,    0,    0,     // 0x68-0x6F
    0,    0,    0,    0,    0,    0,    0,    0,     // 0x70-0x77
    0,    0,    0,    0,    0,    0,    0,    0      // 0x78-0x7F
};

// Shifted ASCII
static const char hid_to_ascii_shift[128] = {
    0,    0,    0,    0,   'A',  'B',  'C',  'D',   // 0x00-0x07
    'E',  'F',  'G',  'H',  'I',  'J',  'K',  'L',   // 0x08-0x0F
    'M',  'N',  'O',  'P',  'Q',  'R',  'S',  'T',   // 0x10-0x17
    'U',  'V',  'W',  'X',  'Y',  'Z',  '!',  '@',   // 0x18-0x1F
    '#',  '$',  '%',  '^',  '&',  '*',  '(',  ')',   // 0x20-0x27
    '\n', 27,   '\b', '\t', ' ',  '_',  '+',  '{',   // 0x28-0x2F
    '}',  '|',  '~',  ':',  '"',  '~',  '<',  '>',   // 0x30-0x37
    '?',  0,    0,    0,    0,    0,    0,    0,     // 0x38-0x3F
    0,    0,    0,    0,    0,    0,    0,    0,     // 0x40-0x47
    0,    0,    0,    0,    0,    0,    0,    0,     // 0x48-0x4F
    0,    0,    0,    0,    '/',  '*',  '-',  '+',   // 0x50-0x57
    '\n', '1',  '2',  '3',  '4',  '5',  '6',  '7',   // 0x58-0x5F
    '8',  '9',  '0',  '.',  0,    0,    0,    '=',   // 0x60-0x67
    0,    0,    0,    0,    0,    0,    0,    0,     // 0x68-0x6F
    0,    0,    0,    0,    0,    0,    0,    0,     // 0x70-0x77
    0,    0,    0,    0,    0,    0,    0,    0      // 0x78-0x7F
};

// Check if a key was in the previous report (for detecting new key presses)
static bool key_was_pressed(uint8_t keycode) {
    for (int i = 0; i < 6; i++) {
        if (last_keyboard_report.keys[i] == keycode) {
            return true;
        }
    }
    return false;
}

// Add a character to the keyboard buffer
static void kb_buffer_push(char c) {
    uint8_t next = (kb_buffer_end + 1) % KB_BUFFER_SIZE;
    if (next != kb_buffer_start) {
        kb_buffer[kb_buffer_end] = c;
        kb_buffer_end = next;
    }
}

// Key repeat state - timer-based (like PS2 keyboards)
static uint8_t repeat_keycode = 0;
static uint64_t repeat_start_tick = 0;      // When key was first pressed
static uint64_t repeat_last_tick = 0;       // When last repeat occurred
static bool repeat_shift = false;
static const uint32_t REPEAT_DELAY_TICKS = 500;  // 500ms initial delay (1000Hz timer)
static const uint32_t REPEAT_RATE_TICKS = 33;    // ~30 chars per second (33ms between repeats)

// Handle key repeat - called every poll, uses real timer ticks
static void handle_key_repeat() {
    if (repeat_keycode == 0) return;
    
    uint64_t now = timer_get_ticks();
    uint64_t elapsed = now - repeat_start_tick;
    
    // Check if past initial delay
    if (elapsed >= REPEAT_DELAY_TICKS) {
        // Check if it's time for another repeat
        if (now - repeat_last_tick >= REPEAT_RATE_TICKS) {
            char c;
            if (repeat_shift) {
                c = hid_to_ascii_shift[repeat_keycode];
            } else {
                c = hid_to_ascii[repeat_keycode];
            }
            if (c != 0) {
                kb_buffer_push(c);
            }
            repeat_last_tick = now;
        }
    }
}

// Process a keyboard report - only called when new data arrives
static void process_keyboard_report(HidKeyboardReport* report) {
    bool shift = (report->modifiers & (HID_MOD_LEFT_SHIFT | HID_MOD_RIGHT_SHIFT)) != 0;
    bool ctrl = (report->modifiers & (HID_MOD_LEFT_CTRL | HID_MOD_RIGHT_CTRL)) != 0;
    
    // Find first currently pressed key
    uint8_t current_key = 0;
    for (int i = 0; i < 6; i++) {
        if (report->keys[i] != 0 && report->keys[i] < 128) {
            current_key = report->keys[i];
            break;
        }
    }
    
    // Check all keys for new presses
    for (int i = 0; i < 6; i++) {
        uint8_t keycode = report->keys[i];
        
        if (keycode == 0) continue;
        if (keycode >= 128) continue;
        
        // Only process if this is a new key press
        if (!key_was_pressed(keycode)) {
            // Handle Shift+Arrow for text selection (HID: 0x50=Left, 0x4F=Right)
            if (shift && keycode == 0x50) {  // Shift+Left
                kb_buffer_push(KEY_SHIFT_LEFT);
                continue;
            }
            if (shift && keycode == 0x4F) {  // Shift+Right
                kb_buffer_push(KEY_SHIFT_RIGHT);
                continue;
            }
            
            char c;
            if (shift) {
                c = hid_to_ascii_shift[keycode];
            } else {
                c = hid_to_ascii[keycode];
            }
            
            // Handle Ctrl key combinations - generate control codes
            if (ctrl && c != 0) {
                // Convert letter to control code (a=1, b=2, ..., z=26)
                if (c >= 'a' && c <= 'z') {
                    kb_buffer_push(c - 'a' + 1);
                    // Start repeat for Ctrl combo
                    repeat_keycode = keycode;
                    repeat_shift = shift;
                    repeat_start_tick = timer_get_ticks();
                    repeat_last_tick = repeat_start_tick;
                    continue;
                }
                if (c >= 'A' && c <= 'Z') {
                    kb_buffer_push(c - 'A' + 1);
                    repeat_keycode = keycode;
                    repeat_shift = shift;
                    repeat_start_tick = timer_get_ticks();
                    repeat_last_tick = repeat_start_tick;
                    continue;
                }
                // Special cases
                if (c == '[' || c == '{') { kb_buffer_push(27); continue; }  // Ctrl+[ = Escape
                if (c == '\\' || c == '|') { kb_buffer_push(28); continue; } // Ctrl+\
                if (c == ']' || c == '}') { kb_buffer_push(29); continue; }  // Ctrl+]
            }
            
            if (c != 0) {
                kb_buffer_push(c);
            }
            
            // Start repeat for this key
            repeat_keycode = keycode;
            repeat_shift = shift;
            repeat_start_tick = timer_get_ticks();
            repeat_last_tick = repeat_start_tick;
        }
    }
    
    // Handle key release
    if (current_key == 0) {
        // All keys released - stop repeating
        repeat_keycode = 0;
    } else if (current_key != repeat_keycode && repeat_keycode != 0) {
        // Different key now held - switch to new key
        // Don't start repeat, wait for it to be detected as "new"
    }
    // Note: repeat counter is incremented in handle_key_repeat(), not here
    
    // Save report for next comparison
    last_keyboard_report = *report;
}

// Process a mouse report - robust 8-bit parsing with length-based Report ID detection
static void process_mouse_report(HidMouseReport* raw_buffer, uint16_t length) {
    if (length < 3) return;

    uint8_t* data = (uint8_t*)raw_buffer;
    uint8_t btn = 0;
    int8_t rel_x = 0;
    int8_t rel_y = 0;
    int8_t wheel = 0;

    // SIMPLIFIED PROTOCOL DETECTION
    // Most mice send 4 bytes: [Btn, X, Y, Wheel]
    // Some send 5 bytes: [ID, Btn, X, Y, Wheel]
    
    // If it looks like a Report ID packet (Byte 0 is 1 or 2, and length is sufficient)
    // Length >= 5 indicates [ID, Btn, X, Y, Wheel] format
    // Length == 4 is almost certainly [Btn, X, Y, Wheel] (standard boot protocol)
    bool has_report_id = (length >= 5) && (data[0] == 1 || data[0] == 2);
    
    if (has_report_id) {
         btn = data[1];
         rel_x = (int8_t)data[2];
         rel_y = (int8_t)data[3];
         if (length >= 5) wheel = (int8_t)data[4];
    } else {
         // Standard Layout: [Btn, X, Y, Wheel?]
         btn = data[0];
         rel_x = (int8_t)data[1];
         rel_y = (int8_t)data[2];
         if (length >= 4) wheel = (int8_t)data[3];
    }

    // Global State Update
    mouse_left = (btn & HID_MOUSE_LEFT) != 0;
    mouse_right = (btn & HID_MOUSE_RIGHT) != 0;
    mouse_middle = (btn & HID_MOUSE_MIDDLE) != 0;
    mouse_scroll += wheel;

    mouse_x += rel_x;
    mouse_y += rel_y;

    // Clamp to screen bounds
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= screen_width) mouse_x = screen_width - 1;
    if (mouse_y >= screen_height) mouse_y = screen_height - 1;

    mouse_available = true;
    mouse_data_received = true;
}

// Set boot protocol for a HID device
static bool set_boot_protocol(UsbDeviceInfo* dev) {
    uint16_t transferred;
    return xhci_control_transfer(
        dev->slot_id,
        0x21,  // Host-to-device, Class, Interface
        HID_REQ_SET_PROTOCOL,
        HID_PROTOCOL_BOOT,
        dev->hid_interface,
        0,
        nullptr,
        &transferred
    );
}

// Set idle rate (0 = only report on change)
static bool set_idle(UsbDeviceInfo* dev, uint8_t idle_rate) {
    uint16_t transferred;
    return xhci_control_transfer(
        dev->slot_id,
        0x21,  // Host-to-device, Class, Interface
        HID_REQ_SET_IDLE,
        idle_rate << 8,
        dev->hid_interface,
        0,
        nullptr,
        &transferred
    );
}

void usb_hid_init() {
    int count = usb_get_device_count();
    
    // Always log this to help diagnose issues
    DEBUG_INFO("HID Init: %d USB devices", count);
    
    for (int i = 0; i < count; i++) {
        UsbDeviceInfo* dev = usb_get_device(i);
        if (!dev || !dev->configured) continue;
        
        // Log device info for debugging
        DEBUG_LOG("Dev %d: Slot %d KBD=%d MOUSE=%d EP1=%d EP2=%d", 
            i, dev->slot_id, dev->is_keyboard ? 1 : 0, dev->is_mouse ? 1 : 0,
            dev->hid_endpoint, dev->hid_endpoint2);
        
        if (dev->is_keyboard) {
            keyboard_available = true;
            keyboard_device = dev;
            
            // Only send SET_PROTOCOL to Boot Interface devices
            if (dev->is_boot_interface && dev->hid_endpoint != 0) {
                if (set_boot_protocol(dev)) {
                    DEBUG_LOG("Slot %d: Keyboard Boot Proto OK", dev->slot_id);
                } else {
                    DEBUG_ERROR("Slot %d: Keyboard Boot Proto FAIL", dev->slot_id);
                }
            }
            
            // SET_IDLE to 100ms (25 * 4ms) to ensure we get periodic reports
            // This helps recover if a "key up" packet is missed
            if (dev->hid_endpoint != 0) {
                set_idle(dev, 25);
            }
        }
        
        // NOTE: Not using else-if so composite devices (keyboard+mouse) initialize both
        if (dev->is_mouse) {
            mouse_available = true;
            mouse_device = dev;
            
            // Determine which endpoint/interface to use for mouse
            uint8_t mouse_ep = (dev->hid_endpoint2 != 0) ? dev->hid_endpoint2 : dev->hid_endpoint;
            uint8_t mouse_iface = (dev->hid_interface2 != 0) ? dev->hid_interface2 : dev->hid_interface;
            
            DEBUG_LOG("Mouse detected: Slot %d EP %d Iface %d Boot=%d", 
                dev->slot_id, mouse_ep, mouse_iface, dev->is_boot_interface ? 1 : 0);
            
            // FIX: Do NOT force Boot Protocol for mice!
            // Gaming mice (1000Hz, high DPI) behave poorly in Boot Protocol.
            // Boot Protocol clamps values to signed 8-bit (-127 to 127),
            // causing "negative acceleration" or cursor sticking on fast movements.
            // Leave mice in Report Protocol (default) for full precision.
            
            // Only set Idle rate to 0 (report on change only) to prevent keep-alive spam
            if (mouse_ep != 0) {
                uint16_t transferred;
                xhci_control_transfer(
                    dev->slot_id,
                    0x21,  // Host-to-device, Class, Interface
                    HID_REQ_SET_IDLE,
                    0,  // idle_rate = 0 (report only on change)
                    mouse_iface,
                    0,
                    nullptr,
                    &transferred
                );
            }
            
            // Center mouse on screen
            mouse_x = screen_width / 2;
            mouse_y = screen_height / 2;
        }
    }
    
    // Summary log
    DEBUG_INFO("HID: Keyboard=%s Mouse=%s", 
        keyboard_available ? "YES" : "NO", 
        mouse_available ? "YES" : "NO");
}

void usb_hid_poll() {
    int count = usb_get_device_count();
    if (count <= 0) return;

    uint64_t now = timer_get_ticks();

    for (int i = 0; i < count; i++) {
        UsbDeviceInfo* dev = usb_get_device(i);
        if (!dev || !dev->configured || dev->slot_id == 0) continue;

        // --- KEYBOARD POLLING ---
        if (dev->is_keyboard && dev->hid_endpoint != 0) {
            uint64_t kb_interval = (dev->hid_interval < 1) ? 10 : dev->hid_interval;
            
            if (now - last_keyboard_poll >= kb_interval) {
                // Buffer must be large enough for largest possible HID report
                uint8_t buffer[64]; 
                uint16_t transferred = 0;

                // Try to read keyboard
                bool success = xhci_interrupt_transfer(
                    dev->slot_id, 
                    dev->hid_endpoint, 
                    buffer, 
                    sizeof(HidKeyboardReport), // Request 8 bytes
                    &transferred
                );

                if (success && transferred >= 3) {
                     // Check if it's actually a keyboard report (8 bytes)
                     if (transferred == 8) {
                         process_keyboard_report((HidKeyboardReport*)buffer);
                     }
                }
                last_keyboard_poll = now;
            }
        }

        // --- MOUSE POLLING ---
        // Only poll if it is a mouse AND has a separate endpoint 
        // OR if it's a mouse-only device.
        uint8_t mouse_ep = 0;
        if (dev->is_mouse) {
             if (dev->hid_endpoint2 != 0) mouse_ep = dev->hid_endpoint2;
             else if (!dev->is_keyboard) mouse_ep = dev->hid_endpoint;
        }

        if (mouse_ep != 0) {
            // Poll mouse - just try ONE packet per loop to be safe
            // This prevents starving the keyboard on composite devices
            
            uint8_t buffer[64];
            uint16_t transferred = 0;
            
            bool success = xhci_interrupt_transfer(
                dev->slot_id, 
                mouse_ep, 
                buffer, 
                64, // Max size
                &transferred
            );

            if (success && transferred >= 3) {
                if (!mouse_data_received) DEBUG_INFO("HID: Mouse Data!");
                process_mouse_report((HidMouseReport*)buffer, transferred);
            }
        }
    }
    
    // Always handle repeat logic outside the loop
    handle_key_repeat();
}

bool usb_hid_keyboard_available() {
    return keyboard_available;
}

bool usb_hid_keyboard_has_char() {
    return kb_buffer_start != kb_buffer_end;
}

char usb_hid_keyboard_get_char() {
    if (kb_buffer_start == kb_buffer_end) {
        return 0;
    }
    char c = kb_buffer[kb_buffer_start];
    kb_buffer_start = (kb_buffer_start + 1) % KB_BUFFER_SIZE;
    return c;
}

bool usb_hid_mouse_available() {
    // Only report available if we've actually received mouse data
    // This allows PS/2 mouse to work as fallback when USB mouse doesn't respond
    return mouse_available && mouse_data_received;
}

void usb_hid_mouse_get_state(int32_t* x, int32_t* y, bool* left, bool* right, bool* middle) {
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
    if (left) *left = mouse_left;
    if (right) *right = mouse_right;
    if (middle) *middle = mouse_middle;
}

void usb_hid_set_screen_size(int32_t width, int32_t height) {
    screen_width = width;
    screen_height = height;
    // Center mouse on screen when dimensions are set
    mouse_x = width / 2;
    mouse_y = height / 2;
}

int8_t usb_hid_mouse_get_scroll() {
    int8_t delta = mouse_scroll;
    mouse_scroll = 0;  // Clear after reading
    return delta;
}

void usb_hid_set_debug(bool enabled) {
    hid_debug = enabled;
}

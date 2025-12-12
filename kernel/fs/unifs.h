#pragma once
#include <stdint.h>
#include <stddef.h>

// ============================================================================
// uniFS - Simple Flat Filesystem for uniOS
// ============================================================================
// Format: Header + Entries[] + Data blob
// - Header: 8-byte magic + 8-byte file count
// - Entry:  64-byte name + 8-byte offset + 8-byte size
// - Data:   Raw file contents concatenated
// ============================================================================

// uniFS magic signature
#define UNIFS_MAGIC "UNIFS v1"

// File type detection (based on extension/content)
#define UNIFS_TYPE_UNKNOWN  0
#define UNIFS_TYPE_TEXT     1
#define UNIFS_TYPE_BINARY   2
#define UNIFS_TYPE_ELF      3

// On-disk structures
struct UniFSHeader {
    char magic[8];        // "UNIFS v1"
    uint64_t file_count;  // Number of files
} __attribute__((packed));

struct UniFSEntry {
    char name[64];        // Null-terminated filename
    uint64_t offset;      // Offset from start of filesystem
    uint64_t size;        // File size in bytes
} __attribute__((packed));

// In-memory file handle
struct UniFSFile {
    const char* name;
    uint64_t size;
    const uint8_t* data;
};

// ============================================================================
// API Functions
// ============================================================================

// Initialize filesystem from memory address (typically from Limine module)
void unifs_init(void* start_addr);

// Check if filesystem is mounted and valid
bool unifs_is_mounted();

// Open a file by name (returns nullptr if not found)
const UniFSFile* unifs_open(const char* name);

// Check if a file exists
bool unifs_file_exists(const char* name);

// Get file size by name (returns 0 if not found)
uint64_t unifs_get_file_size(const char* name);

// Get file type (UNIFS_TYPE_*)
int unifs_get_file_type(const char* name);

// Get total number of files
uint64_t unifs_get_file_count();

// Get filename by index
const char* unifs_get_file_name(uint64_t index);

// Get file size by index
uint64_t unifs_get_file_size_by_index(uint64_t index);

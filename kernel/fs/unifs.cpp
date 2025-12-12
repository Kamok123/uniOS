#include "unifs.h"
#include "kstring.h"

// ============================================================================
// uniFS Implementation
// ============================================================================

static uint8_t* fs_start = nullptr;
static UniFSHeader* header = nullptr;
static UniFSEntry* entries = nullptr;
static bool mounted = false;

// ELF magic bytes
static const uint8_t ELF_MAGIC[] = {0x7F, 'E', 'L', 'F'};

// ============================================================================
// Internal Helpers
// ============================================================================

// Find entry by name (returns nullptr if not found)
static UniFSEntry* find_entry(const char* name) {
    if (!mounted || !name) return nullptr;
    
    for (uint64_t i = 0; i < header->file_count; i++) {
        if (kstring::strcmp(entries[i].name, name) == 0) {
            return &entries[i];
        }
    }
    return nullptr;
}

// Check if file content looks like text
static bool is_text_content(const uint8_t* data, uint64_t size) {
    // Check first 256 bytes (or less if file is smaller)
    uint64_t check_size = (size < 256) ? size : 256;
    
    for (uint64_t i = 0; i < check_size; i++) {
        uint8_t c = data[i];
        // Allow printable ASCII, newline, tab, carriage return
        if (c < 32 && c != '\n' && c != '\r' && c != '\t') {
            return false;
        }
        if (c > 126 && c < 160) {
            return false;  // Non-printable extended ASCII
        }
    }
    return true;
}

// ============================================================================
// API Implementation
// ============================================================================

void unifs_init(void* start_addr) {
    if (!start_addr) {
        mounted = false;
        return;
    }
    
    fs_start = (uint8_t*)start_addr;
    header = (UniFSHeader*)fs_start;
    entries = (UniFSEntry*)(fs_start + sizeof(UniFSHeader));
    
    // Verify magic
    if (kstring::memcmp(header->magic, UNIFS_MAGIC, 8) != 0) {
        mounted = false;
        return;
    }
    
    mounted = true;
}

bool unifs_is_mounted() {
    return mounted;
}

const UniFSFile* unifs_open(const char* name) {
    UniFSEntry* entry = find_entry(name);
    if (!entry) return nullptr;
    
    // Return static file handle
    static UniFSFile file;
    file.name = entry->name;
    file.size = entry->size;
    file.data = fs_start + entry->offset;
    return &file;
}

bool unifs_file_exists(const char* name) {
    return find_entry(name) != nullptr;
}

uint64_t unifs_get_file_size(const char* name) {
    UniFSEntry* entry = find_entry(name);
    return entry ? entry->size : 0;
}

int unifs_get_file_type(const char* name) {
    UniFSEntry* entry = find_entry(name);
    if (!entry) return UNIFS_TYPE_UNKNOWN;
    
    const uint8_t* data = fs_start + entry->offset;
    uint64_t size = entry->size;
    
    // Check for ELF magic
    if (size >= 4 && kstring::memcmp(data, ELF_MAGIC, 4) == 0) {
        return UNIFS_TYPE_ELF;
    }
    
    // Check if content looks like text
    if (is_text_content(data, size)) {
        return UNIFS_TYPE_TEXT;
    }
    
    return UNIFS_TYPE_BINARY;
}

uint64_t unifs_get_file_count() {
    return mounted ? header->file_count : 0;
}

const char* unifs_get_file_name(uint64_t index) {
    if (!mounted || index >= header->file_count) return nullptr;
    return entries[index].name;
}

uint64_t unifs_get_file_size_by_index(uint64_t index) {
    if (!mounted || index >= header->file_count) return 0;
    return entries[index].size;
}

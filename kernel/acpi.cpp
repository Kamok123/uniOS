#include "acpi.h"
#include "io.h"
#include "vmm.h"
#include "graphics.h"

// ACPI state
static bool acpi_available = false;
static uint32_t pm1a_cnt = 0;      // PM1a Control port
static uint32_t pm1b_cnt = 0;      // PM1b Control port (optional)
static uint16_t slp_typa = 0;      // Sleep type value for S5
static uint16_t slp_typb = 0;      // Sleep type value for S5 (optional)
static uint32_t smi_cmd_port = 0;  // SMI command port
static uint8_t acpi_enable_val = 0; // Value to write to enable ACPI

// Sleep enable bit
#define ACPI_SLP_EN  (1 << 13)

// QEMU/Bochs shutdown port (fallback)
#define QEMU_SHUTDOWN_PORT  0x604
#define QEMU_SHUTDOWN_VALUE 0x2000

// Validate RSDP checksum
static bool rsdp_checksum_valid(const AcpiRsdp* rsdp) {
    const uint8_t* bytes = (const uint8_t*)rsdp;
    uint8_t sum = 0;
    for (int i = 0; i < 20; i++) {
        sum += bytes[i];
    }
    return sum == 0;
}

// Validate SDT checksum
static bool sdt_checksum_valid(const AcpiSdtHeader* header) {
    const uint8_t* bytes = (const uint8_t*)header;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < header->length; i++) {
        sum += bytes[i];
    }
    return sum == 0;
}

// Search for RSDP in memory range
static AcpiRsdp* find_rsdp_in_range(uint64_t start, uint64_t end) {
    // RSDP must be 16-byte aligned
    for (uint64_t addr = start; addr < end; addr += 16) {
        uint8_t* ptr = (uint8_t*)vmm_phys_to_virt(addr);
        
        // Check "RSD PTR " signature
        if (ptr[0] == 'R' && ptr[1] == 'S' && ptr[2] == 'D' && ptr[3] == ' ' &&
            ptr[4] == 'P' && ptr[5] == 'T' && ptr[6] == 'R' && ptr[7] == ' ') {
            AcpiRsdp* rsdp = (AcpiRsdp*)ptr;
            if (rsdp_checksum_valid(rsdp)) {
                return rsdp;
            }
        }
    }
    return nullptr;
}

// Find RSDP (Root System Description Pointer)
static AcpiRsdp* find_rsdp() {
    // Search in Extended BIOS Data Area (EBDA) - typically at 0x9FC00-0x9FFFF
    // The EBDA address is at 0x40E (word, segment address)
    uint16_t ebda_segment = *(uint16_t*)vmm_phys_to_virt(0x40E);
    uint64_t ebda_addr = (uint64_t)ebda_segment << 4;
    
    AcpiRsdp* rsdp = find_rsdp_in_range(ebda_addr, ebda_addr + 0x400);
    if (rsdp) return rsdp;
    
    // Search in BIOS ROM area (0xE0000-0xFFFFF)
    return find_rsdp_in_range(0xE0000, 0x100000);
}

// Parse DSDT/SSDT to find \_S5 sleep type values
// This is a simplified parser - real ACPI requires full AML parsing
static bool find_s5_in_dsdt(uint64_t dsdt_phys) {
    AcpiSdtHeader* dsdt = (AcpiSdtHeader*)vmm_phys_to_virt(dsdt_phys);
    
    if (!sdt_checksum_valid(dsdt)) {
        return false;
    }
    
    // Search for "_S5_" in the DSDT
    uint8_t* data = (uint8_t*)dsdt;
    uint32_t length = dsdt->length;
    
    for (uint32_t i = sizeof(AcpiSdtHeader); i < length - 4; i++) {
        // Look for "_S5_" package
        if (data[i] == '_' && data[i+1] == 'S' && data[i+2] == '5' && data[i+3] == '_') {
            // Found \_S5_ - now parse the package
            // Skip ahead to find the package data
            // The structure is typically: "_S5_" + NameOp(0x12) + PkgLength + NumElements + ByteData...
            uint32_t j = i + 4;
            
            // Skip any AML prefix opcodes
            while (j < length && (data[j] == 0x08 || data[j] == 0x12)) {
                j++;
            }
            
            // Look for PackageOp (0x12) or directly find byte values
            while (j < length - 2) {
                if (data[j] == 0x12) {
                    // Package found, skip package length
                    j++;
                    if ((data[j] & 0xC0) == 0) j += 1;
                    else if ((data[j] & 0xC0) == 0x40) j += 2;
                    else if ((data[j] & 0xC0) == 0x80) j += 3;
                    else j += 4;
                    
                    // Skip element count
                    j++;
                    
                    // First element is SLP_TYPa
                    if (data[j] == 0x0A) {  // BytePrefix
                        slp_typa = data[j + 1] << 10;
                        j += 2;
                    } else if (data[j] == 0x0B) {  // WordPrefix
                        slp_typa = (data[j + 1] | (data[j + 2] << 8)) << 10;
                        j += 3;
                    } else if (data[j] < 64) {  // Small integer
                        slp_typa = data[j] << 10;
                        j++;
                    }
                    
                    // Second element is SLP_TYPb (optional)
                    if (j < length) {
                        if (data[j] == 0x0A) {
                            slp_typb = data[j + 1] << 10;
                        } else if (data[j] < 64) {
                            slp_typb = data[j] << 10;
                        }
                    }
                    
                    return true;
                }
                j++;
            }
        }
    }
    
    // Fallback: use common default values
    // Most systems use SLP_TYP = 5 for S5 (sleep type 5)
    slp_typa = 5 << 10;
    slp_typb = 5 << 10;
    return true;
}

void acpi_init() {
    // Find RSDP
    AcpiRsdp* rsdp = find_rsdp();
    if (!rsdp) {
        gfx_draw_string(10, 10, "ACPI: RSDP not found", COLOR_GRAY);
        return;
    }
    
    // Get RSDT or XSDT
    uint64_t rsdt_phys;
    bool use_xsdt = false;
    
    if (rsdp->revision >= 2) {
        AcpiRsdp20* rsdp20 = (AcpiRsdp20*)rsdp;
        if (rsdp20->xsdt_address != 0) {
            rsdt_phys = rsdp20->xsdt_address;
            use_xsdt = true;
        } else {
            rsdt_phys = rsdp->rsdt_address;
        }
    } else {
        rsdt_phys = rsdp->rsdt_address;
    }
    
    AcpiSdtHeader* rsdt = (AcpiSdtHeader*)vmm_phys_to_virt(rsdt_phys);
    if (!sdt_checksum_valid(rsdt)) {
        gfx_draw_string(10, 10, "ACPI: RSDT checksum failed", COLOR_GRAY);
        return;
    }
    
    // Find FADT in RSDT/XSDT entries
    uint32_t entries = (rsdt->length - sizeof(AcpiSdtHeader)) / (use_xsdt ? 8 : 4);
    uint8_t* entry_base = (uint8_t*)rsdt + sizeof(AcpiSdtHeader);
    
    for (uint32_t i = 0; i < entries; i++) {
        uint64_t table_phys;
        if (use_xsdt) {
            table_phys = *(uint64_t*)(entry_base + i * 8);
        } else {
            table_phys = *(uint32_t*)(entry_base + i * 4);
        }
        
        AcpiSdtHeader* table = (AcpiSdtHeader*)vmm_phys_to_virt(table_phys);
        
        // Check for FACP (FADT signature in ACPI)
        if (table->signature[0] == 'F' && table->signature[1] == 'A' &&
            table->signature[2] == 'C' && table->signature[3] == 'P') {
            
            AcpiFadt* fadt = (AcpiFadt*)table;
            pm1a_cnt = fadt->pm1a_cnt_blk;
            pm1b_cnt = fadt->pm1b_cnt_blk;
            smi_cmd_port = fadt->smi_cmd;
            acpi_enable_val = fadt->acpi_enable;
            
            // Parse DSDT to find S5 sleep type
            if (fadt->dsdt) {
                find_s5_in_dsdt(fadt->dsdt);
            }
            
            acpi_available = true;
            
            // Debug: show ACPI status using graphics (before shell starts)
            char buf[64];
            buf[0] = 'A'; buf[1] = 'C'; buf[2] = 'P'; buf[3] = 'I';
            buf[4] = ':'; buf[5] = ' '; buf[6] = 'P'; buf[7] = 'M';
            buf[8] = '1'; buf[9] = 'a'; buf[10] = '='; buf[11] = '0';
            buf[12] = 'x';
            // Simple hex conversion
            uint16_t val = pm1a_cnt;
            for (int i = 0; i < 4; i++) {
                int nibble = (val >> (12 - i*4)) & 0xF;
                buf[13 + i] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
            }
            buf[17] = 0;
            gfx_draw_string(10, gfx_get_height() - 40, buf, COLOR_GRAY);
            
            return;
        }
    }
    
    gfx_draw_string(10, 10, "ACPI: FADT not found", COLOR_GRAY);
}

bool acpi_is_available() {
    return acpi_available;
}

// SCI_EN bit in PM1_CNT register
#define ACPI_SCI_EN (1 << 0)

bool acpi_poweroff() {
    if (!acpi_available || pm1a_cnt == 0) {
        // Try QEMU/Bochs fallback
        outw(QEMU_SHUTDOWN_PORT, QEMU_SHUTDOWN_VALUE);
        
        // If we're still running, QEMU method didn't work
        return false;
    }
    
    // Disable interrupts
    asm volatile("cli");
    
    // Check if ACPI is already enabled (SCI_EN bit set in PM1a_CNT)
    uint16_t pm1_value = inw(pm1a_cnt);
    if (!(pm1_value & ACPI_SCI_EN)) {
        // ACPI is not enabled, need to enable it via SMI command
        if (smi_cmd_port != 0 && acpi_enable_val != 0) {
            outb(smi_cmd_port, acpi_enable_val);
            
            // Wait for ACPI to become enabled
            for (int i = 0; i < 1000; i++) {
                pm1_value = inw(pm1a_cnt);
                if (pm1_value & ACPI_SCI_EN) break;
                for (volatile int j = 0; j < 10000; j++); // Small delay
            }
        }
    }
    
    // Try our parsed SLP_TYPa first
    if (slp_typa != 0) {
        outw(pm1a_cnt, slp_typa | ACPI_SLP_EN);
        for (volatile int i = 0; i < 10000000; i++);
    }
    
    // Try common SLP_TYP values for S5 (bits 10-12)
    // Different systems use different values
    static const uint16_t common_slp_types[] = {
        (5 << 10),  // Most common: SLP_TYP = 5
        (7 << 10),  // SLP_TYP = 7 (some systems)
        (0 << 10),  // SLP_TYP = 0 (older systems)
        (6 << 10),  // SLP_TYP = 6
    };
    
    for (int i = 0; i < 4; i++) {
        outw(pm1a_cnt, common_slp_types[i] | ACPI_SLP_EN);
        for (volatile int j = 0; j < 10000000; j++);
        
        if (pm1b_cnt != 0) {
            outw(pm1b_cnt, common_slp_types[i] | ACPI_SLP_EN);
            for (volatile int j = 0; j < 10000000; j++);
        }
    }
    
    // Try QEMU fallback
    outw(QEMU_SHUTDOWN_PORT, QEMU_SHUTDOWN_VALUE);
    
    // Try older Bochs port
    outw(0xB004, 0x2000);
    
    return false;
}

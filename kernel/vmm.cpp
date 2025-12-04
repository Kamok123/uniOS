#include "vmm.h"
#include "pmm.h"
#include "limine.h"

// Limine HHDM request (Higher Half Direct Map)
__attribute__((used, section(".requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

// Limine Kernel Address request
__attribute__((used, section(".requests")))
static volatile struct limine_kernel_address_request kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};

static uint64_t* pml4 = nullptr;
static uint64_t hhdm_offset = 0;

static uint64_t* get_next_level(uint64_t* current_level, uint64_t index, bool alloc) {
    if (current_level[index] & PTE_PRESENT) {
        uint64_t phys = current_level[index] & 0x000FFFFFFFFFF000;
        return (uint64_t*)(phys + hhdm_offset);
    }

    if (!alloc) return nullptr;

    void* frame = pmm_alloc_frame();
    if (!frame) return nullptr;

    uint64_t phys = (uint64_t)frame;
    current_level[index] = phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    
    uint64_t* next_level = (uint64_t*)(phys + hhdm_offset);
    for (int i = 0; i < 512; i++) next_level[i] = 0; // Clear new page table
    
    return next_level;
}

void vmm_init() {
    if (hhdm_request.response == NULL) return;
    hhdm_offset = hhdm_request.response->offset;

    // Get current CR3 (Physical address of PML4)
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    
    // Access PML4 via HHDM
    pml4 = (uint64_t*)(cr3 + hhdm_offset);
}

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t pml4_index = (virt >> 39) & 0x1FF;
    uint64_t pdpt_index = (virt >> 30) & 0x1FF;
    uint64_t pd_index   = (virt >> 21) & 0x1FF;
    uint64_t pt_index   = (virt >> 12) & 0x1FF;

    uint64_t* pdpt = get_next_level(pml4, pml4_index, true);
    if (!pdpt) return;

    uint64_t* pd = get_next_level(pdpt, pdpt_index, true);
    if (!pd) return;

    uint64_t* pt = get_next_level(pd, pd_index, true);
    if (!pt) return;

    pt[pt_index] = phys | flags;
    
    // Invalidate TLB
    asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

uint64_t vmm_virt_to_phys(uint64_t virt) {
    // TODO: Walk page tables to find physical address
    return 0; 
}

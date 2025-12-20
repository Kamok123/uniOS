#include "scheduler.h"
#include "process.h"
#include "heap.h"
#include "pmm.h"
#include "debug.h"
#include "spinlock.h"
#include "timer.h"
#include <stddef.h>
#include <string.h>  // For memset if available, else we'll do it manually

// External assembly function to initialize FPU state
extern "C" void init_fpu_state(uint8_t* fpu_buffer);

// Scheduler lock for thread safety
static Spinlock scheduler_lock = SPINLOCK_INIT;

// Kernel stack size - 16KB to handle deep call chains (networking, etc.)
#define KERNEL_STACK_SIZE 16384

static Process* current_process = nullptr;
static Process* process_list = nullptr;
static uint64_t next_pid = 1;

Process* process_get_current() {
    return current_process;
}

Process* process_find_by_pid(uint64_t pid) {
    Process* p = process_list;
    if (!p) return nullptr;
    
    do {
        if (p->pid == pid) return p;
        p = p->next;
    } while (p != process_list);
    
    return nullptr;
}

void scheduler_init() {
    DEBUG_INFO("Initializing Scheduler...\n");
    
    // Create a process struct for the current running kernel thread (idle task)
    current_process = (Process*)malloc(sizeof(Process));
    if (!current_process) {
        panic("Failed to allocate initial process!");
    }
    
    // Zero the entire struct first
    uint8_t* p = (uint8_t*)current_process;
    for (size_t i = 0; i < sizeof(Process); i++) p[i] = 0;
    
    current_process->pid = 0;
    current_process->parent_pid = 0;
    current_process->sp = 0;              // Not used for idle task
    current_process->stack_base = nullptr; // Not used for idle task
    current_process->page_table = nullptr; // Kernel tasks share kernel page table
    current_process->state = PROCESS_RUNNING;
    current_process->exit_status = 0;
    current_process->wait_for_pid = 0;
    current_process->next = current_process; // Circular list
    
    // Initialize FPU state for idle task
    init_fpu_state(current_process->fpu_state);
    current_process->fpu_initialized = true;
    
    process_list = current_process;
    
    DEBUG_INFO("Scheduler Initialized. Initial PID: 0\n");
}

void scheduler_create_task(void (*entry)()) {
    Process* new_process = (Process*)malloc(sizeof(Process));
    if (!new_process) {
        DEBUG_ERROR("Failed to allocate process struct\n");
        return;
    }
    
    // Zero the entire struct first
    uint8_t* p = (uint8_t*)new_process;
    for (size_t i = 0; i < sizeof(Process); i++) p[i] = 0;
    
    new_process->pid = next_pid++;
    new_process->parent_pid = current_process ? current_process->pid : 0;
    new_process->state = PROCESS_READY;
    new_process->exit_status = 0;
    new_process->wait_for_pid = 0;
    new_process->page_table = nullptr; // Kernel task
    
    // Initialize FPU state for the new task
    init_fpu_state(new_process->fpu_state);
    new_process->fpu_initialized = true;
    
    // Allocate stack (16KB for deep call chains like networking)
    new_process->stack_base = (uint64_t*)malloc(KERNEL_STACK_SIZE);
    if (!new_process->stack_base) {
        DEBUG_ERROR("Failed to allocate stack for PID %d\n", new_process->pid);
        free(new_process);
        return; 
    }
    
    // Align stack top to 16 bytes
    uint64_t stack_addr = (uint64_t)new_process->stack_base + KERNEL_STACK_SIZE;
    stack_addr &= ~0xF; 
    uint64_t* stack_top = (uint64_t*)stack_addr;
    
    // Set up initial stack for switch_to_task
    stack_top--; *stack_top = 0; // Dummy return
    stack_top--; *stack_top = (uint64_t)entry; // RIP
    stack_top--; *stack_top = 0x202; // RFLAGS
    
    // Callee-saved regs
    for (int i = 0; i < 6; i++) {
        stack_top--; *stack_top = 0;
    }
    
    new_process->sp = (uint64_t)stack_top;
    
    // Add to list (protected by scheduler lock)
    spinlock_acquire(&scheduler_lock);
    Process* last = process_list;
    while (last->next != process_list) {
        last = last->next;
    }
    last->next = new_process;
    new_process->next = process_list;
    spinlock_release(&scheduler_lock);
    
    DEBUG_INFO("Created Task PID: %d\n", new_process->pid);
}

// Helper: Wake up any sleeping processes whose time has come
static void wake_sleeping_processes() {
    uint64_t now = timer_get_ticks();
    Process* p = process_list;
    if (!p) return;
    
    do {
        if (p->state == PROCESS_SLEEPING && now >= p->wake_time) {
            p->state = PROCESS_READY;
        }
        p = p->next;
    } while (p != process_list);
}

void scheduler_schedule() {
    if (!current_process) return;
    
    // Disable interrupts during scheduling to prevent reentrancy
    // Note: We don't use spinlock here because we can't hold it across context switch
    uint64_t flags = interrupts_save_disable();
    
    // Wake up any sleeping processes
    wake_sleeping_processes();
    
    Process* next = current_process->next;
    Process* start = next;
    
    // Find next runnable process
    do {
        if (next->state == PROCESS_READY || next->state == PROCESS_RUNNING) {
            break;
        }
        next = next->next;
    } while (next != start);
    
    if (next == current_process || 
        (next->state != PROCESS_READY && next->state != PROCESS_RUNNING)) {
        interrupts_restore(flags);
        return;
    }
    
    Process* prev = current_process;
    if (prev->state == PROCESS_RUNNING) {
        prev->state = PROCESS_READY;
    }
    
    current_process = next;
    current_process->state = PROCESS_RUNNING;
    
    // DO NOT restore interrupts here!
    // The switch_to_task will pushfq (saving disabled state for prev) and popfq
    // (restoring the next task's RFLAGS, which includes its IF flag).
    // This prevents a race where an interrupt fires between updating current_process
    // and actually switching stacks.
    (void)flags;  // flags is saved by pushfq in switch_to_task
    
    switch_to_task(prev, current_process);
}

void scheduler_yield() {
    scheduler_schedule();
}

// Fork: Create a copy of current process
uint64_t process_fork() {
    Process* parent = current_process;
    Process* child = (Process*)malloc(sizeof(Process));
    if (!child) return (uint64_t)-1;
    
    // Zero the child struct first
    uint8_t* p = (uint8_t*)child;
    for (size_t i = 0; i < sizeof(Process); i++) p[i] = 0;
    
    spinlock_acquire(&scheduler_lock);
    child->pid = next_pid++;
    spinlock_release(&scheduler_lock);
    
    child->parent_pid = parent->pid;
    child->state = PROCESS_READY;
    child->exit_status = 0;
    child->wait_for_pid = 0;
    
    // Copy parent's FPU state
    for (size_t i = 0; i < FPU_STATE_SIZE; i++) {
        child->fpu_state[i] = parent->fpu_state[i];
    }
    child->fpu_initialized = true;
    
    // Allocate new stack (16KB)
    child->stack_base = (uint64_t*)malloc(KERNEL_STACK_SIZE);
    if (!child->stack_base) {
        free(child);
        return (uint64_t)-1;
    }
    
    // Copy parent's stack
    // Note: This is a simplified fork. In a real OS with VMM, we'd use COW.
    size_t stack_qwords = KERNEL_STACK_SIZE / sizeof(uint64_t);
    for (size_t i = 0; i < stack_qwords; i++) {
        child->stack_base[i] = parent->stack_base[i];
    }
    
    // Adjust child's SP
    uint64_t stack_offset = parent->sp - (uint64_t)parent->stack_base;
    child->sp = (uint64_t)child->stack_base + stack_offset;
    
    // Share page table for now (since we don't have full user process loading yet)
    // TODO: Implement COW page table cloning
    child->page_table = parent->page_table;
    
    // Add to list (protected by scheduler lock)
    spinlock_acquire(&scheduler_lock);
    Process* last = process_list;
    while (last->next != process_list) {
        last = last->next;
    }
    last->next = child;
    child->next = process_list;
    spinlock_release(&scheduler_lock);
    
    DEBUG_INFO("Forked PID %d -> %d\n", parent->pid, child->pid);
    return child->pid;
}

void process_exit(int32_t status) {
    DEBUG_INFO("Process %d exiting with status %d\n", current_process->pid, status);
    
    current_process->state = PROCESS_ZOMBIE;
    current_process->exit_status = status;
    
    // Wake up parent if waiting
    Process* parent = process_find_by_pid(current_process->parent_pid);
    if (parent && parent->state == PROCESS_WAITING) {
        if (parent->wait_for_pid == 0 || parent->wait_for_pid == current_process->pid) {
            parent->state = PROCESS_READY;
        }
    }
    
    scheduler_schedule();
    for(;;);
}

int64_t process_waitpid(int64_t pid, int32_t* status) {
    while (true) {
        // Look for zombie child
        Process* p = process_list;
        do {
            if (p->parent_pid == current_process->pid && p->state == PROCESS_ZOMBIE) {
                if (pid == -1 || (uint64_t)pid == p->pid) {
                    // Found zombie
                    if (status) *status = p->exit_status;
                    uint64_t child_pid = p->pid;
                    
                    // Mark as cleaned up (BLOCKED for now, effectively removed from scheduling)
                    p->state = PROCESS_BLOCKED; 
                    
                    DEBUG_INFO("Reaped zombie PID %d\n", child_pid);
                    return child_pid;
                }
            }
            p = p->next;
        } while (p != process_list);
        
        // No zombie found, wait
        current_process->state = PROCESS_WAITING;
        current_process->wait_for_pid = (pid == -1) ? 0 : pid;
        scheduler_schedule();
    }
}

// Sleep current process for a given number of timer ticks
void scheduler_sleep(uint64_t ticks) {
    if (!current_process) return;
    
    uint64_t flags = interrupts_save_disable();
    
    current_process->wake_time = timer_get_ticks() + ticks;
    current_process->state = PROCESS_SLEEPING;
    
    interrupts_restore(flags);
    
    // Yield to let another process run
    scheduler_schedule();
}

// Sleep current process for a given number of milliseconds
void scheduler_sleep_ms(uint64_t ms) {
    uint32_t freq = timer_get_frequency();
    // Convert ms to ticks: ticks = ms * freq / 1000
    // Avoid overflow by doing division first for large ms values
    uint64_t ticks = (ms * freq) / 1000;
    if (ticks == 0 && ms > 0) ticks = 1;  // At least 1 tick for non-zero ms
    scheduler_sleep(ticks);
}

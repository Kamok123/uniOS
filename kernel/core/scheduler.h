#pragma once
#include <stdint.h>

void scheduler_init();
void scheduler_create_task(void (*entry)());
void scheduler_schedule();
void scheduler_yield();

// Sleep for a number of timer ticks (blocks the current process)
void scheduler_sleep(uint64_t ticks);

// Sleep for milliseconds (convenience wrapper)
void scheduler_sleep_ms(uint64_t ms);

#pragma once
#include <stdint.h>

#define SYS_WRITE 1
#define SYS_EXIT  60

extern "C" uint64_t syscall_handler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3);

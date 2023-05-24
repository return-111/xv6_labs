#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"

void
backtrace(void)
{
    uint64 p = r_fp();
    printf("backtrace:\n");
    while (PGROUNDUP(p) - PGROUNDDOWN(p) == PGSIZE) {
        printf("%p\n", *((uint64 *)(p - 8)));
        p = *((uint64 *)(p - 16));
    }
}
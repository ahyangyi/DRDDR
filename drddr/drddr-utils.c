#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/spinlock.h>
#include <linux/version.h>
#include "disassem.h"
#include "drddr-utils.h"

static size_t idt_table_addr;
static size_t save_stack_trace_regs_addr;

int set_memory_rw(unsigned long addr, int numpages);
int set_memory_ro(unsigned long addr, int numpages);

void drddr_memory_rw (size_t p)
{
    set_memory_rw(((long long)p) & ~((1 << 12) - 1), 1);
}

void drddr_memory_ro (size_t p)
{
    set_memory_ro(((long long)p) & ~((1 << 12) - 1), 1);
}

size_t drddr_get_handler(long long offset)
{
    unsigned short addr_low = *(unsigned short*)(idt_table_addr + offset * 16);
    unsigned short addr_mid = *(unsigned short*)(idt_table_addr + offset * 16 + 6);
    unsigned int addr_high = *(unsigned int*)(idt_table_addr + offset * 16 + 8);

    printk ("IDT at %08x, offset= %d\n", (unsigned int)idt_table_addr, (int)offset);
    printk ("Handler at %04x%02x%02x\n", (unsigned int)addr_high, (unsigned int)addr_mid, (unsigned int)addr_low);

    return (((unsigned long long)(addr_high)) << 32) +
        (((unsigned long long)(addr_mid)) << 16) +
        (((unsigned long long)(addr_low)));
}

long long drddr_read_value (size_t addr, int length)
{
    if (length == 8)
        return *(char *)(addr);
    if (length == 16)
        return *(short *)(addr);
    if (length == 32)
        return *(int *)(addr);
    if (length == 64)
        return *(long long *)(addr);
    return 0;
}

void drddr_util_init (void)
{
    idt_table_addr = kallsyms_lookup_name("idt_table");
    save_stack_trace_regs_addr = kallsyms_lookup_name("save_stack_trace_regs");

    printk ("save_stack_trace_regs address at %016lx\n", save_stack_trace_regs_addr);
}

void drddr_stack_trace_regs (struct pt_regs *regs, struct drddr_trace *trace)
{
    trace->trace.max_entries = TRACE_SIZE;
    trace->trace.nr_entries = 0;
    trace->trace.entries = trace->trace_buf;
    trace->trace.skip = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0)
    ((void (*)(struct stack_trace *, struct pt_regs *))save_stack_trace_regs_addr)(&(trace->trace), regs);
#else
    ((void (*)(struct pt_regs *, struct stack_trace *))save_stack_trace_regs_addr)(regs, &(trace->trace));
#endif
}

void drddr_print_trace (struct drddr_trace *trace)
{
    print_stack_trace(&(trace->trace), 3);
}

void drddr_dump (struct pt_regs* regs)
{
    printk ("    AX %016lx CX %016lx BX %016lx DX %016lx\n", regs->ax, regs->cx, regs->bx, regs->dx);
    printk ("    SI %016lx DI %016lx BP %016lx SP %016lx\n", regs->si, regs->di, regs->bp, regs->sp);
    printk ("    R8 %016lx R9 %016lx 10 %016lx 11 %016lx\n", regs->r8, regs->r9, regs->r10,regs->r11);
    printk ("    12 %016lx 13 %016lx 14 %016lx 15 %016lx\n", regs->r12,regs->r13,regs->r14,regs->r15);
}

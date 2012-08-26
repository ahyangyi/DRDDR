#include <linux/ptrace.h>
#include <linux/stacktrace.h>

/*
 * Constant definitions
 */
#define TRACE_SIZE 32

/*
 * Type definitions
 */
struct drddr_trace
{
    struct stack_trace trace;
    unsigned long trace_buf[TRACE_SIZE];
};

/*
 * Functions
 */

size_t drddr_get_handler(long long offset);
void drddr_memory_rw (size_t p);
void drddr_memory_ro (size_t p);
long long drddr_read_value (size_t addr, int length);
void drddr_util_init (void);
void drddr_stack_trace_regs (struct pt_regs *regs, struct drddr_trace *trace);
void drddr_print_trace (struct drddr_trace *trace);
void drddr_dump (struct pt_regs* regs);

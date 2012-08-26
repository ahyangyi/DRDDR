#ifndef DISASSEM_H
#define DISASSEM_H

#define MEM_PER_INSTR 16

void yy_get_memory_access (struct pt_regs * regs, size_t* data_addr, short* data_length, bool* is_write);

#endif

#include <stdarg.h>
#include <assert.h>

typedef char bool;

struct pt_regs {
        unsigned long r15;
        unsigned long r14;
        unsigned long r13;
        unsigned long r12;
        unsigned long bp;
        unsigned long bx;
/* arguments: non interrupts/non tracing syscalls only save up to here*/
        unsigned long r11;
        unsigned long r10;
        unsigned long r9;
        unsigned long r8;
        unsigned long ax;
        unsigned long cx;
        unsigned long dx;
        unsigned long si;
        unsigned long di;
        unsigned long orig_ax;
/* end of arguments */
/* cpu exception frame or undefined */
        unsigned long ip;
        unsigned long cs;
        unsigned long flags;
        unsigned long sp;
        unsigned long ss;
/* top of stack page */
};

int printk (const char* fmt, ...)
{
    va_list args;
    int r;

    va_start(args, fmt);
    r = vprintf(fmt, args);
    va_end(args);

    return r;
}

#include <stdio.h>
#include "disassem.h"

size_t data_addr[MEM_PER_INSTR];
short data_length[MEM_PER_INSTR];
bool is_write[MEM_PER_INSTR];

void zero_assert (short* a, int length)
{
    int i;

    for (i = 0; i < length; i ++)
        assert(a[i] == 0);
}

void test0 ()
{
    struct pt_regs regs;
    char inst[] = {0xff, 0x47, 0x5c};
    int temp[100];
   
    temp[0x5c / 4] = 23;
    
    regs.ip = (unsigned long)inst;
    regs.di = (unsigned long)temp;

    yy_get_memory_access (&regs, data_addr, data_length, is_write);

    assert (data_addr[0] == 0x5c + (size_t)temp);
    assert (data_length[0] == 4);
    assert (is_write[0] == 1);
    zero_assert (data_length + 1, MEM_PER_INSTR - 1);

    //CWDE
    inst[0] = 0x98;
    yy_get_memory_access (&regs, data_addr, data_length, is_write);
    zero_assert (data_length, MEM_PER_INSTR);
    
    //CDQ
    inst[0] = 0x99;
    yy_get_memory_access (&regs, data_addr, data_length, is_write);
    zero_assert (data_length, MEM_PER_INSTR);
    
    printf ("Test case 0 succeed.\n");
}

/*
 * dummy tests: a collection of tests from the program DUMMY in my CACHEAPP.
 */
void dummy_tests ()
{
    struct pt_regs regs;
    char inst[10];
    int data32[32];

    // push %rbp
    inst[0] = 0x55;
    regs.sp = 0xdeadbeef;
    regs.bp = 0xbadcafe;

    yy_get_memory_access(&regs, data_addr, data_length, is_write);
    
    assert (data_addr[0] == regs.sp - 8);
    assert (data_length[0] == 8);
    assert (is_write[0] == 1);
    zero_assert (data_length + 1, MEM_PER_INSTR - 1);

    // push %rbx
    inst[0] = 0x53;
    regs.sp = 0xdeadbeef;
    regs.bx = 0xbadcafe;

    yy_get_memory_access(&regs, data_addr, data_length, is_write);
    
    assert (data_addr[0] == regs.sp - 8);
    assert (data_length[0] == 8);
    assert (is_write[0] == 1);
    zero_assert (data_length + 1, MEM_PER_INSTR - 1);

    // push %rbx
    inst[0] = 0x53;
    regs.sp = 0xdeadbeef;
    regs.bx = 0xbadcafe;

    yy_get_memory_access(&regs, &data_addr, &data_length, &is_write);
    
    assert (data_addr == regs.sp - 8);
    assert (data_length == 8);
    assert (is_write == 1);

    // mov %edi, -0x24(%rbp)
    inst[0] = 0x89;
    inst[1] = 0x7d;
    inst[2] = 0xdc;
    regs.bp = 0xdeadbeef;
    
    yy_get_memory_access(&regs, data_addr, data_length, is_write);

    assert (data_addr[0] = 0xdeadbeef - 0x24);
    assert (data_length[0] == 4);
    assert (is_write[0] == 1);
    zero_assert (data_length + 1, MEM_PER_INSTR - 1);

    // mov %rsi, -0x30(%rbp)
    inst[0] = 0x48;
    inst[1] = 0x89;
    inst[2] = 0x75;
    inst[3] = 0xd0;
    regs.bp = 0xdeadbeef;
    
    yy_get_memory_access(&regs, data_addr, data_length, is_write);

    assert (data_addr[0] = 0xdeadbeef - 0x30);
    assert (data_length[0] == 8);
    assert (is_write[0] == 1);
    zero_assert (data_length + 1, MEM_PER_INSTR - 1);

    // cmpl $0x2, -0x24(%rbp)
    inst[0] = 0x83;
    inst[1] = 0x7d;
    inst[2] = 0xdc;
    inst[3] = 0x02;
    regs.bp = 0xdeadbeef;
    
    yy_get_memory_access(&regs, data_addr, data_length, is_write);

    assert (data_addr[0] = 0xdeadbeef - 0x24);
    assert (data_length[0] == 4);
    assert (is_write[0] == 0);
    zero_assert (data_length + 1, MEM_PER_INSTR - 1);

    // mov -0x30(%rbp),%rax
    inst[0] = 0x48;
    inst[1] = 0x8b;
    inst[2] = 0x45;
    inst[3] = 0xd0;
    regs.bp = 0xdeadbeef;
    
    yy_get_memory_access(&regs, data_addr, data_length, is_write);

    assert (data_addr[0] = 0xdeadbeef - 0x30);
    assert (data_length[0] == 8);
    assert (is_write[0] == 0);
    zero_assert (data_length + 1, MEM_PER_INSTR - 1);

    // mov (%rax), %rax
    inst[0] = 0x48;
    inst[1] = 0x8b;
    inst[2] = 0x00;
    regs.ax = 0xbadf00d;
    
    yy_get_memory_access(&regs, data_addr, data_length, is_write);

    assert (data_addr[0] = 0xbadf00d);
    assert (data_length[0] == 8);
    assert (is_write[0] == 0);
    zero_assert (data_length + 1, MEM_PER_INSTR - 1);

    // lea -0x20(%rbp),%rdx
    inst[0] = 0x48;
    inst[1] = 0x8d;
    inst[2] = 0x55;
    inst[3] = 0xe0;
    regs.bp = 0xbadcafe;
    
    yy_get_memory_access(&regs, data_addr, data_length, is_write);

    zero_assert (data_length, MEM_PER_INSTR);

    // movl $0x1000,-0x20(%rbp)
    inst[0] = 0xc7;
    inst[1] = 0x45;
    inst[2] = 0xe0;
    inst[3] = 0x00;
    inst[4] = 0x10;
    inst[5] = 0x00;
    inst[6] = 0x00;
    regs.bp = 0xbee;
    
    yy_get_memory_access(&regs, data_addr, data_length, is_write);

    assert (data_addr[0] = 0xbee - 0x20);
    assert (data_length[0] == 4);
    assert (is_write[0] == 1);
    zero_assert (data_length + 1, MEM_PER_INSTR - 1);

    // movl $0x1,0x601060(, %rax, 4)
    inst[0] = 0xc7;
    inst[1] = 0x04;
    inst[2] = 0x85;
    inst[3] = 0x60;
    inst[4] = 0x10;
    inst[5] = 0x60;
    inst[6] = 0x00;

    inst[7] = 0x10;
    inst[8] = 0x00;
    inst[9] = 0x00;
    inst[10]= 0x00;
    regs.ax = 0xacecafe;
    
    yy_get_memory_access(&regs, data_addr, data_length, is_write);

    assert (data_addr[0] = 0x601060+regs.ax*4);
    assert (data_length[0] == 4);
    assert (is_write[0] == 1);
    zero_assert (data_length + 1, MEM_PER_INSTR - 1);

    printf ("Dummy test succeed.\n");
}

int main ()
{
    test0 ();
    dummy_tests ();

    return 0;
}

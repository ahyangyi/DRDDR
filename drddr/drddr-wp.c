#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/kallsyms.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/version.h>
#include "drddr-wp.h"
#include "drddr-utils.h"

struct watch {
    unsigned long long dr0;
    unsigned long long dr1;
    unsigned long long dr2;
    unsigned long long dr3;
    unsigned long long ctrl;
};
static bool wp_usage[4];
static size_t wp_addr[4];
static int wp_length[4];
static int wp_iswrite[4];
static struct drddr_trace wp_trace[4];
static struct pt_regs wp_regs[4]; 
static int wp_cpuid[4];
static spinlock_t wp_lock_drddr;
static int wp_total_count;
static int wp_num[4];

int wp_len_to_flag (int length)
{
    return length == 8? 0 : length == 16? 1 : length == 32? 3 : 2;
}

int wp_rw_to_flag (bool iswrite)
{
    return iswrite? 3 : 1;
}

void wp_set_to_register(void * _watches)
{
    struct watch *watches = (struct watch*) _watches;

    if (watches->dr0)
        __asm__ __volatile__ (  "movq %0,%%dr0   \n\t" 
                                : 
                                : "r" (watches->dr0)    );

    if (watches->dr1)
        __asm__ __volatile__ (  "movq %0,%%dr1   \n\t"
                                :
                                : "r" (watches->dr1)    );

    if (watches->dr2)
        __asm__ __volatile__ (  "movq %0,%%dr2   \n\t"
                                :
                                : "r" (watches->dr2)    );

    if (watches->dr3)
        __asm__ __volatile__ (  "movq %0,%%dr3   \n\t"
                                :
                                : "r" (watches->dr3)    );

    /* set ctrl */
    __asm__ __volatile__ (  "movq %0,%%dr7  \n\t"
                            :
                            : "r" (watches->ctrl)   );
}

void set_dr (void)
{
    struct watch w;

#define TRAP_GLOBAL_DR0 (1<<1)
#define TRAP_GLOBAL_DR1 (1<<3)
#define TRAP_GLOBAL_DR2 (1<<5)
#define TRAP_GLOBAL_DR3 (1<<7)

/* exact instruction detection not supported on P6 */
#define TRAP_LE         (1<<8)
#define TRAP_GE         (1<<9)

/* Global Detect flag */
#define GD_ACCESS       (1<<13)

/* 2 bits R/W and 2 bits len from these offsets */
#define DR0_RW      16
#define DR0_LEN     18
#define DR1_RW      20
#define DR1_LEN     22
#define DR2_RW      24
#define DR2_LEN     26
#define DR3_RW      28
#define DR3_LEN     30

    w.ctrl = TRAP_LE | TRAP_GE;

    if (wp_usage[0])
    {
        w.dr0 = wp_addr[0];
        w.ctrl |= TRAP_GLOBAL_DR0;
        w.ctrl |= (wp_rw_to_flag (wp_iswrite[0]) << DR0_RW);
        w.ctrl |= (wp_len_to_flag (wp_length[0]) << DR0_LEN);
    } else w.dr0 = 0;

    if (wp_usage[1])
    {
        w.dr1 = wp_addr[1];
        w.ctrl |= TRAP_GLOBAL_DR1;
        w.ctrl |= (wp_rw_to_flag (wp_iswrite[1]) << DR1_RW);
        w.ctrl |= (wp_len_to_flag (wp_length[1]) << DR1_LEN);
    } else w.dr1 = 0;

    if (wp_usage[2])
    {
        w.dr2 = wp_addr[2];
        w.ctrl |= TRAP_GLOBAL_DR2;
        w.ctrl |= (wp_rw_to_flag (wp_iswrite[2]) << DR2_RW);
        w.ctrl |= (wp_len_to_flag (wp_length[2]) << DR2_LEN);
    } else w.dr2 = 0;

    if (wp_usage[3])
    {
        w.dr3 = wp_addr[3];
        w.ctrl |= TRAP_GLOBAL_DR3;
        w.ctrl |= (wp_rw_to_flag (wp_iswrite[3]) << DR3_RW);
        w.ctrl |= (wp_len_to_flag (wp_length[3]) << DR3_LEN);
    } else w.dr3 = 0;

/*
   printk ("The watch to set: %08x%08x %08x%08x %08x%08x %08x%08x CTRL %08x%08x\n",
            (int)(w.dr0 >> 32), (int)w.dr0,
            (int)(w.dr1 >> 32), (int)w.dr1,
            (int)(w.dr2 >> 32), (int)w.dr2,
            (int)(w.dr3 >> 32), (int)w.dr3,
            (int)(w.ctrl >> 32), (int)w.ctrl
            );
*/  
   on_each_cpu(
	wp_set_to_register
	, &w, 1);
}

struct watchpoint_id wp_add (size_t data_addr, int data_length, bool is_write, struct pt_regs *regs)
{
    struct watchpoint_id id;

    if (!wp_usage[0] || !wp_usage[1] || !wp_usage[2] || !wp_usage[3])
    {
        spin_lock(&wp_lock_drddr);
        if (!wp_usage[0] || !wp_usage[1] || !wp_usage[2] || !wp_usage[3])
        {
            int min_index = 0;

            while (wp_usage[min_index])
                min_index ++;

            wp_usage[min_index] = true;
            wp_addr[min_index] = data_addr;
            wp_length[min_index] = data_length;
            wp_iswrite[min_index] = is_write;
            wp_regs[min_index] = *regs;
            wp_num[min_index] = ++wp_total_count;
            wp_cpuid[min_index] = smp_processor_id();
            drddr_stack_trace_regs (regs, &(wp_trace[min_index]));

            printk ("setting watchpoint at DR%d (address=%08x%08x)\n", min_index, (int)(data_addr >> 32), (int)data_addr);

            set_dr();

            spin_unlock(&wp_lock_drddr);

            id.dr = min_index;
            id.num = wp_total_count;
            return id;
        }
        spin_unlock(&wp_lock_drddr);
    }

    watchpoint_id_set_invalid(&id);
    return id;
}

void wp_remove (struct watchpoint_id id)
{
    spin_lock(&wp_lock_drddr);

    if (id.num == 0 || id.num == wp_num[id.dr])
    {
        wp_usage[id.dr] = false;
        set_dr();
    }

    spin_unlock(&wp_lock_drddr);
}

void wp_init (void)
{
    spin_lock_init(&wp_lock_drddr);
    wp_usage[0] = wp_usage[1] = wp_usage[2] = wp_usage[3] = false;
    set_dr();
    wp_total_count = 0;
}

void wp_clean (void)
{
    spin_lock(&wp_lock_drddr);
    wp_usage[0] = wp_usage[1] = wp_usage[2] = wp_usage[3] = false;
    set_dr();
    spin_unlock(&wp_lock_drddr);
}

struct watchpoint_id wp_query (void)
{
    unsigned long re;
    struct watchpoint_id id = { .num = 0 };
    int i;

    __asm__ __volatile__ (  "movq %%dr6,%0   \n\t"
                            : "=r" (re)    );
    for (i = 0; i < 4; i ++)
        if (re & (1 << i)) {
            id.dr = i;
            return id;
        }
    watchpoint_id_set_invalid(&id);
    return id;
}

void wp_report (struct watchpoint_id id)
{
    printk ("Information of the watchpoint %d:\n", id.dr);
    printk ("    Instruction address: %016lx\n", (unsigned long)wp_regs[id.dr].ip);
    printk ("    Accessed address:    %016lx\n", (unsigned long)wp_addr[id.dr]);
    printk ("    Access type:         %s\n", wp_iswrite[id.dr]? "write" : "read");
    printk ("    Access length:       %d\n", wp_length[id.dr]);
    printk ("    CPU ID:              %d\n", wp_cpuid[id.dr]);
    drddr_dump (&wp_regs[id.dr]);
    printk ("Stack trace:\n");
    drddr_print_trace (&(wp_trace[id.dr]));
}

void wp_monitor (char *s, size_t limit, size_t *cur_pos)
{
#define PRINT(...) do{(*cur_pos)+=sprintf(s+*cur_pos, __VA_ARGS__);} while(0)

    int i;

    PRINT ("Watchpoint status:\n");

    for (i = 0; i < 4; i ++)
    {
        if (wp_usage[i])
        {
            PRINT ("    DR%d watching %16lx len %d mode %c\n",
                    i, (long)wp_addr[i], wp_length[i], 
                    (char)(wp_iswrite[i]?'W':'R')
                    );
        }
        else
            PRINT ("    DR%d unused\n", i);
    }

#undef PRINT
}

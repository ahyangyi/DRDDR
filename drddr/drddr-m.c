/*
 * drddr = Data Race Detector using Debug Registers
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <linux/random.h>
#include <linux/debugfs.h>
#include "disassem.h"
#include "drddr-utils.h"
#include "drddr-wp.h"
#include "drddr-bp.h"
#include "drddr.h"

/*
 * Input
 */
static unsigned long address [N_ADDRESS];
static unsigned int u_addresscnt;

module_param_array(address, ulong, &u_addresscnt, 0000);
MODULE_PARM_DESC(address, "ADDRESS LIST");

/*
 * Controller status
 */
bool running;

/*
 * Debugfs files
 */
struct dentry *d_dir;
struct dentry *d_ctrl;
struct dentry *d_address;
struct dentry *d_addresscnt;
struct dentry *d_param;
struct dentry *d_monitor;
struct dentry *d_log;

int fd_ctrl;
int fd_address;
int fd_monitor;

static int i_param;

static ssize_t read_ctrl(struct file *file, char __user *userbuf,
        size_t count, loff_t *ppos);
static ssize_t write_ctrl(struct file *file, const char __user *buf,
        size_t count, loff_t *ppos);

static ssize_t read_address(struct file *file, char __user *userbuf,
        size_t count, loff_t *ppos);
static ssize_t write_address(struct file *file, const char __user *buf,
        size_t count, loff_t *ppos);

static ssize_t read_monitor(struct file *file, char __user *userbuf,
        size_t count, loff_t *ppos);

static const struct file_operations ops_ctrl = {
    .read = read_ctrl,
    .write = write_ctrl,
};

static const struct file_operations ops_address = {
    .read = read_address,
    .write = write_address,
};

static const struct file_operations ops_monitor = {
    .read = read_monitor,
};

char mybuf[CMD_SIZE];

/*
 * Debug handlers
 */

static size_t old_do_debug, old_do_int3;

static size_t swap_handler(size_t handler, int my_do_something)
{
    unsigned char *p        = (unsigned char *)handler;
    unsigned char buf[4]    = "\x00\x00\x00\x00";
    unsigned int offset     = 0;
    unsigned int orig       = 0;

    /* find a candidate for the call .. needs better heuristics */
    while (p[0] != 0xe8)
    {
        p ++;
    }

    // save_paranoid here

    p ++;
    while (p[0] != 0xe8)
    {
        p ++;
    }
    printk("*** found call do_debug %X\n", (unsigned int)(size_t)p);
    buf[0]  = p[1];
    buf[1]  = p[2];
    buf[2]  = p[3];
    buf[3]  = p[4];

    offset  = *(unsigned int *)buf; 
    printk("*** found call do_debug offset %X\n", offset);

    orig    = offset + (unsigned int)((size_t)p) + 5;
    printk("*** original do_debug %X\n", orig);

    offset  = my_do_something - (unsigned int)((size_t)p) - 5;
    printk("*** want call do_debug offset %X\n", offset);

    drddr_memory_rw ((size_t)p + 1);
    drddr_memory_rw ((size_t)p + 4);
    p[1]    = (offset & 0x000000ff);
    p[2]    = (offset & 0x0000ff00) >>  8;
    p[3]    = (offset & 0x00ff0000) >> 16;
    p[4]    = (offset & 0xff000000) >> 24;
    drddr_memory_ro ((size_t)p + 1);
    drddr_memory_ro ((size_t)p + 4);

    printk("*** patched in new do_debug offset\n");

    return 0xffffffff00000000ll + orig;
}

typedef void (*int_handler_drddr)(struct pt_regs * regs, 
        unsigned long error_code);

static void get_data_info (struct pt_regs* regs, size_t* data_addr, short* data_length, bool* is_write)
{
    yy_get_memory_access (regs, data_addr, data_length, is_write);
    *data_length *= 8;
}

static void int3_real_work (struct pt_regs* regs)
{
    size_t data_addr[MEM_PER_INSTR];
    short data_length[MEM_PER_INSTR];
    bool is_write[MEM_PER_INSTR];

    get_data_info(regs,data_addr,data_length,is_write);

    if (data_length[0] > 0)
    {
        unsigned long sleep_time;
        long long old_value, new_value;
        struct watchpoint_id wp_id;
//        wait_queue_head_t timeout_wq;

//        old_value = drddr_read_value(data_addr[0], data_length[0]);

        wp_id = wp_add(data_addr[0], data_length[0], is_write[0], regs);

//        init_waitqueue_head(&timeout_wq);
//            interruptible_sleep_on_timeout(&timeout_wq, HZ * sleep_time / 100); //Wait for 0.01~0.15 sec.

        if (watchpoint_id_is_valid(&wp_id))
        {
            sleep_time = random32() % 15 + 1;
            sleep_time = jiffies + sleep_time * HZ / 100;
            while (time_before(jiffies, sleep_time))
                ;
            wp_remove(wp_id);
        }

//        new_value = drddr_read_value(data_addr[0], data_length[0]);

//        printk ("%08x%08x -> %08x%08x\n", (int)(old_value >> 32), (int)(old_value), (int)(new_value >> 32), (int)(new_value));
    }

    bp_add(-1);

    bp_dec_trapped ();
}

static void int3_actual_work (void)
{
/*
 * The x86-64 calling convention:
 * %rdi, %rsi, %rdx, %rcx, %r8, %r9
 */
    __asm__ __volatile__ (
        "leaq    0x10(%%rbp),%%rdi\n\t"
        "callq  %P1\n\t"
        "movq    0x38(%%rbp),%%rbx\n\t"
        "movq    0x40(%%rbp),%%r11\n\t"
        "movq    0x48(%%rbp),%%r10\n\t"
        "movq    0x50(%%rbp),%%r9\n\t"
        "movq    0x58(%%rbp),%%r8\n\t"
        "movq    0x68(%%rbp),%%rcx\n\t"
        "movq    0x70(%%rbp),%%rdx\n\t"
        "movq    0x78(%%rbp),%%rsi\n\t"
        "movq    0x80(%%rbp),%%rdi\n\t"
        "leaq    0xa0(%%rbp),%%rsp\n\t"
        "popfq  \n\t"
        "movq    0x0(%%rbp),%%rax\n\t"
        "movq    %%rax,0xa8(%%rbp)\n\t"
        "movq    0x8(%%rbp),%%rax\n\t"
        "movq    %%rax,0xb0(%%rbp)\n\t"
        "movq    0x60(%%rbp),%%rax\n\t"
        "leaq    0xa8(%%rbp),%%rbp\n\t"
        "leaveq\n\t"
        "retq\n\t"
        :
        : "i" (get_data_info), "i" (int3_real_work)
        );
}

static void int3_drddr(struct pt_regs * regs, 
        unsigned long error_code)
{
    printk ("int3!! at %016lx\n", regs -> ip - 1);

    if (bp_check((size_t)(regs -> ip) - 1))
    {
        regs -> ip --;
        if (bp_remove(regs -> ip))
        {
            if (!bp_inc_trapped())
                return;
            printk ("brk at %08x%08x triggered!\n", (int)(regs -> ip >> 32), (int)(regs -> ip));


            /*
               printk ("%02x %02x %02x %02x %02x %02x %02x %02x\n", 
             *(unsigned char *)(regs -> ip),
             ((unsigned char *)(regs -> ip))[1],
             ((unsigned char *)(regs -> ip))[2],
             ((unsigned char *)(regs -> ip))[3],
             ((unsigned char *)(regs -> ip))[4],
             ((unsigned char *)(regs -> ip))[5],
             ((unsigned char *)(regs -> ip))[6],
             ((unsigned char *)(regs -> ip))[7]);

             printk ("REGS %08x %08x %08x %08x %08x %08x %08x %08x\n", 
             (int)(regs -> ax),
             (int)(regs -> cx),
             (int)(regs -> bx),
             (int)(regs -> dx),
             (int)(regs -> sp),
             (int)(regs -> bp),
             (int)(regs -> si),
             (int)(regs -> di)
             );
             */
            // the following code should not reference brk_* any more
            regs -> sp -= sizeof (struct pt_regs);
            *(struct pt_regs* )regs -> sp = *regs;
            regs -> sp -= 8;
            *(size_t* )regs -> sp = regs -> ip;
            regs -> ip = (size_t)int3_actual_work;

            return;
        }
        return;
    }

    ((int_handler_drddr)old_do_int3)(regs, error_code);
}

static void stop (void);

static void start (void)
{
    if (running)
        stop();

    printk ("DRDDR: starting...\n");

    wp_clean();
    bp_init (address, u_addresscnt);

    if (i_param == -1)
    {
        if (u_addresscnt > 0)
            bp_add (0);
    }
    else if (i_param <= -2 && i_param >= -65536)
    {
        int i;

        for (i = 0; i < u_addresscnt; i -= i_param)
            bp_add(i);
    }
    else
    {
        int i;

        for (i = 0; i < i_param; i ++)
            bp_add (-1);
    }

    i_param = -1;

    running = true;
}

static void stop (void)
{
    if (!running) return;

    printk ("DRDDR: stopping...\n");

    bp_clean();
    wp_clean();

    running = false;
}

static void debug_drddr(struct pt_regs * regs, 
        unsigned long error_code)
{
    struct watchpoint_id wp_id;

    wp_id = wp_query();

    printk ("***************************************************************************\n");
    printk ("Debug interruption!!!!! (OUCH!)\n");

    if (watchpoint_id_is_valid(&wp_id))
    {
        struct drddr_trace trace;

        wp_report (wp_id);

        printk ("---------------------------------------------------------------------------\n");

        printk ("Information of the triggering kernel thread:\n");
        printk ("    Instruction address +1: %016lx\n", regs->ip);

        drddr_dump (regs);

        printk ("Stack trace:\n");
        
        drddr_stack_trace_regs (regs, &trace);
        drddr_print_trace (&trace);

        wp_remove (wp_id);
    }
    else
    {
        printk ("  * This doesn't seem to be caused by debug register.\n");
    }
    printk ("***************************************************************************\n");

    return;

    ((int_handler_drddr)old_do_debug)(regs, error_code);
}

static void drddr_files_init (void)
{
    d_dir = debugfs_create_dir("drddr", NULL);

    d_ctrl = debugfs_create_file("ctrl", 0644, d_dir, &fd_ctrl, &ops_ctrl);
    d_address = debugfs_create_file("address", 0644, d_dir, &fd_address, &ops_address);
    d_monitor = debugfs_create_file("monitor", 0644, d_dir, &fd_monitor, &ops_monitor);
    d_addresscnt = debugfs_create_u32("addresscnt", 0644, d_dir, &u_addresscnt);
    d_param = debugfs_create_x32("param", 0644, d_dir, &i_param);
}

static void drddr_files_clean (void)
{
    debugfs_remove (d_ctrl);
    debugfs_remove (d_address);
    debugfs_remove (d_addresscnt);
    debugfs_remove (d_param);
    debugfs_remove (d_monitor);

    debugfs_remove (d_dir);
}

static void update_status (void)
{
    int i;

    for (i = 0; i < CMD_SIZE; i ++)
    {
        if (!(mybuf[i] >= 0x20 && mybuf[i] <= 0xff))
        {
            mybuf[i] = 0x00;
        }
    }
    mybuf[CMD_SIZE - 1] = 0x00;

    if (strcmp (mybuf, "start") == 0)
    {
        printk ("DRDDR: received command \"start\"\n");
        start();
        return;
    }
    if (strcmp (mybuf, "stop") == 0)
    {
        printk ("DRDDR: received command \"stop\"\n");
        stop();
        return;
    }

    printk ("Cannot understand command: ");
    for (i = 0; i < CMD_SIZE && mybuf[i]; i ++)
    {
        printk ("%c", mybuf[i]);
    }
    printk ("\n");
}

static ssize_t read_ctrl(struct file *file, char __user *userbuf,
        size_t count, loff_t *ppos)
{
    return simple_read_from_buffer(userbuf, count, ppos, mybuf, strlen(mybuf) + 1);
}

static ssize_t write_ctrl(struct file *file, const char __user *buf,
        size_t count, loff_t *ppos)
{
    if(count >= CMD_SIZE)
        return -EINVAL;

    if (copy_from_user(mybuf, buf, count) != 0)
    {
        printk ("DRDDR: write-to-file: copy_from_user() failed\n");
        return -EINVAL;
    }
    mybuf[count]=0;

    update_status ();

    return count;
}

static ssize_t read_address(struct file *file, char __user *userbuf,
        size_t count, loff_t *ppos)
{
    return -EINVAL;
}

static ssize_t write_address(struct file *file, const char __user *buf,
        size_t count, loff_t *ppos)
{
    printk ("write_address() called, count=%lu, \n", (unsigned long)count);

//    if(count > N_ADDRESS * sizeof(size_t))
//        return -EINVAL;

    return simple_write_to_buffer(address, N_ADDRESS * sizeof(size_t), ppos,
        buf, count);
}

static ssize_t read_monitor(struct file *file, char __user *userbuf,
        size_t count, loff_t *ppos)
{
    return 15;
}

static int __init init_drddr(void)
{
    printk(KERN_INFO "drddr init.\n");

    // Controller status initialize
    running = false;
    i_param = -1;
    if (u_addresscnt > 0)
    {
        printk ("Received %u addresses from the command line\n", u_addresscnt);
    }

    // Initialize utils.
    drddr_util_init();

    // Initialize the watchpoint subsystem
    wp_init();

    // Change the brk (int3) handler, and backup the original
    old_do_int3 = swap_handler(drddr_get_handler(3), (int)((size_t)(int3_drddr)));

    // Change the debug (int 1) handler, and backup the original
    old_do_debug = swap_handler(drddr_get_handler(1), (int)((size_t)(debug_drddr)));

    // Initialize files
    drddr_files_init();

    printk(KERN_INFO "DRDDR: initialization finished.\n");

    return 0;
}

static void __exit exit_drddr(void)
{
    printk(KERN_INFO "DRDDR: exit.\n");

    // Clean the files
    drddr_files_clean ();

    // Stop the module from running
    stop();

    // Revert the debug (int 1) handler
    swap_handler(drddr_get_handler(1), old_do_debug);

    // Revert the brk (int3) handler
    swap_handler(drddr_get_handler(3), old_do_int3);
}

module_init(init_drddr);
module_exit(exit_drddr);

MODULE_LICENSE("GPL");


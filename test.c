/*
 * A test driver
 * 
 * For doing various testing.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/kallsyms.h>

static size_t idt_table; 
static size_t innocent;

static int __init init_the_test(void)
{
    printk ("The_test init. %08x\n", (int)&idt_table);
    printk ("%08x\n", (int) kallsyms_lookup_name("idt_table"));
    printk ("The innocent address: %08x\n", (int)&innocent);

	return 0;
}

static void __exit exit_the_test(void)
{
    printk ("The_test exit.\n");
}

module_init(init_the_test);
module_exit(exit_the_test);

MODULE_LICENSE("GPL");


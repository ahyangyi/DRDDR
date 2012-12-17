/*
 * A Write-Write race 
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/kallsyms.h>

static struct task_struct *write1, *write2;

int write_racer_1 (void *vptr)
{
	int generator = 0xab8acada;
	volatile int *ptr = (volatile int *) vptr;
	int i;

	printk ("** WRITER1 ADDRESS: %016Lx. **\n", (0x12ll + ((long long) &&__write1)));
	

	while(1)
	{
		for (i = 0; i < (unsigned int)(generator) % 32; i ++)
		{
			msleep(100);
			if (kthread_should_stop()) goto ouch;
		}

		printk ("Write %08x\n", generator);

__write1:
		*ptr = generator;

		generator = (generator ^ 0xbabeface) * (generator ^ 0xfadedcab);
	}
ouch:
	printk (KERN_INFO "Writer1 exits\n");
	return 0;
}

int write_racer_2 (void *vptr)
{
	int generator = 0xbaddcafe;
	volatile int *ptr = (volatile int *) vptr;
	int i;

	printk ("** WRITER2 ADDRESS: %016Lx. **\n", (0x12ll + ((long long) &&__write2)));
	
	while(1)
	{
		for (i = 0; i < (unsigned int)(generator) % 32; i ++)
		{
			msleep(79);
			if (kthread_should_stop()) goto ouch;
		}

		printk ("Write %08x\n", generator);

__write2:
		*ptr = generator;

		generator = (generator ^ 0xbabeface) * (generator ^ 0xfadedcab);
	}
ouch:
	printk (KERN_INFO "Writer1 exits\n");
	return 0;
}

static int __init wwrace_init(void)
{
	static volatile int ptr = 1;

    printk ("** DATA ADDRESS %16Lx **\n", (int64_t)(&ptr));

	write1 = kthread_run(write_racer_1, (void *)&ptr,  "WRITER 1");
	write2 = kthread_run(write_racer_2, (void *)&ptr,  "WRITER 2");

	return 0;
}

static void __exit wwrace_exit(void)
{
	kthread_stop(write1);
	kthread_stop(write2);
}

module_init(wwrace_init);
module_exit(wwrace_exit);

MODULE_LICENSE("GPL");


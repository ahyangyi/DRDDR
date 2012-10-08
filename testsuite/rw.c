/*
 * A Read-Write race 
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/kallsyms.h>

static struct task_struct *writer, *reader;

int write_racer (void *vptr)
{
	int generator = 0xab8acada;
    volatile int *ptr = (volatile int *) vptr;
	int i;

	printk ("** WRITER ADDRESS: %016Lx. **\n", (0x12ll + ((long long) &&__write)));
	

	while(1)
	{
		for (i = 0; i < (unsigned int)(generator) % 32; i ++)
		{
			msleep(100);
			if (kthread_should_stop()) goto ouch;
		}

		printk ("Write %08x\n", generator);

__write:
		*ptr = generator;

		generator = (generator ^ 0xbabeface) * (generator ^ 0xfadedcab);
	}
ouch:
	printk (KERN_INFO "Writer exits\n");
	return 0;
}

int read_racer (void *vptr)
{
	volatile int *ptr = (volatile int *) vptr;
	int i;
	int read_result;

    printk ("** WRITER ADDRESS: %016Lx. **\n", ((long long) &&__read));

	while(1)
	{
		for (i = 0; i < 16; i ++)
		{
			msleep(80);
			if (kthread_should_stop()) goto ouch2;
		}

__read:
		read_result = *ptr;

		printk ("Read %08x\n", read_result);
	}

ouch2:
	printk (KERN_INFO "Reader exits\n");
	return 0;
}

static int __init rwrace_init(void)
{
	static volatile int ptr = 0;

    printk ("** DATA ADDRESS %16Lx **\n", (int64_t)(&ptr));

	writer = kthread_run(write_racer, (void *)&ptr,  "WRITER");
	reader = kthread_run(read_racer, (void *)&ptr,  "READER");

	return 0;
}

static void __exit rwrace_exit(void)
{
	kthread_stop(writer);
	kthread_stop(reader);
}

module_init(rwrace_init);
module_exit(rwrace_exit);

MODULE_LICENSE("GPL");


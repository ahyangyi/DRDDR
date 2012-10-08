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

static struct task_struct *write1r, *write2er;

int write1_racer (void *vptr)
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

int write2_racer (void *vptr)
{
	int generator = 0xcaca0bea;
	volatile int *ptr = (volatile int *) vptr;
	int i;

    printk ("** WRITER2 ADDRESS: %016Lx. **\n", 0x12ll + ((long long) &&__write2));

	while(1)
	{
		for (i = 0; i < (unsigned int)(generator) % 32; i ++)
		{
			msleep(79);
			if (kthread_should_stop()) goto ouch2;
		}

__write2:
		*ptr = generator;

		generator = (generator ^ 0xbabeface) * (generator ^ 0xfadedcab);
	}

ouch2:
	printk (KERN_INFO "Writer2 exits\n");
	return 0;
}

static int __init rwrace_init(void)
{
	static volatile int ptr = 0;

    printk ("** DATA ADDRESS %16Lx **\n", (int64_t)(&ptr));

	write1r = kthread_run(write1_racer, (void *)&ptr,  "WRITER");
	write2er = kthread_run(write2_racer, (void *)&ptr,  "READER");

	return 0;
}

static void __exit rwrace_exit(void)
{
	kthread_stop(write1r);
	kthread_stop(write2er);
}

module_init(rwrace_init);
module_exit(rwrace_exit);

MODULE_LICENSE("GPL");


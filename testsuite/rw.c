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

int write_racer (void *vcurrent_cafe)
{
	int flavour = 0xab8acada;
	volatile int *current_cafe = (volatile int *) vcurrent_cafe;
	int i;

	printk ("** WRITER ADDRESS: %016Lx. **\n", (0x12ll + ((long long) &&__write)));
	

	while(1)
	{
		for (i = 0; i < (unsigned int)(flavour) % 32; i ++)
		{
			msleep(100);
			if (kthread_should_stop()) goto ouch;
		}

		printk ("The current coffee becomes type %08x\n", flavour);

__write:
		*current_cafe = flavour;

		flavour = (flavour ^ 0xbabeface) * (flavour ^ 0xfadedcab);
	}
ouch:
	printk (KERN_INFO "Welcome again!\n");
	return 0;
}

int read_racer (void *vcurrent_cafe)
{
	volatile int *current_cafe = (volatile int *) vcurrent_cafe;
	int i;
	int cafe_type;

    printk ("** WRITER ADDRESS: %016Lx. **\n", ((long long) &&__read));

	while(1)
	{
		for (i = 0; i < 16; i ++)
		{
			msleep(80);
			if (kthread_should_stop()) goto ouch2;
		}

__read:
		cafe_type = *current_cafe;

		printk ("The current coffee tastes like type %08x\n", cafe_type);
	}

ouch2:
	printk (KERN_INFO "Any more coffee for me to taste?\n");
	return 0;
}

static int __init rwrace_init(void)
{
	static volatile int current_cafe = 0;

    printk ("** DATA ADDRESS %16Lx **\n", (int64_t)(&current_cafe));

	writer = kthread_run(write_racer, (void *)&current_cafe,  "CAFE_MACHINE");
	reader = kthread_run(read_racer, (void *)&current_cafe,  "CAFE_TESTER");

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


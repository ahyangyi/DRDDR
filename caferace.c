#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/kallsyms.h>

static struct task_struct *seller, *buyer;

int cafe_machine (void *vcurrent_cafe)
{
	int flavour = 0xab8acada;
	volatile int *current_cafe = (volatile int *) vcurrent_cafe;
	int i;

	union { long long l; int i[2]; } temp;

	temp.l = (0x14ll + ((long long) &&__write));
	printk ("** WRITER ADDRESS: %08x%08x. **\n", temp.i[1], temp.i[0]);
	

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
__written:

		flavour = (flavour ^ 0xbabeface) * (flavour ^ 0xfadedcab);
	}
ouch:
	printk (KERN_INFO "Welcome again!\n");
	return 0;
}

int cafe_taster (void *vcurrent_cafe)
{
	volatile int *current_cafe = (volatile int *) vcurrent_cafe;
	int i;
	int cafe_type;

	union { long long l; int i[2]; } temp;
    volatile char* volatile p;

    p = (char *)&&__read;
/*
    while (!(*p == 0x44 && *(p+1) == 0x89 && ((*(p+2))&1)))
    {
        p ++;
    }
*/
	temp.l = (long long)p;
	printk ("** READER ADDRESS: %08x%08x. **\n", temp.i[1], temp.i[0]);

	while(1)
	{
		for (i = 0; i < 16; i ++)
		{
			msleep(80);
			if (kthread_should_stop()) goto ouch2;
		}

__read:
		cafe_type = *current_cafe;
__read_done:

		printk ("The current coffee tastes like type %08x\n", cafe_type);
	}

ouch2:
	printk (KERN_INFO "Any more coffee for me to taste?\n");
	return 0;
}

static int __init caferace_init(void)
{
	static volatile int current_cafe = 0;

    union { long long l; int i[2]; } temp;

    temp.l = (size_t)&current_cafe;
    printk ("** DATA ADDRESS %08x%08x **\n", temp.i[1], temp.i[0]);


	temp.l = (long long) kallsyms_lookup_name("ecryptfs_i_size_init");
	printk (KERN_INFO "ecryptfs_i_size_init() at %08x%08x\n", temp.i[1], temp.i[0]);
	temp.l = (long long) kallsyms_lookup_name("ecryptfs_write");
	printk (KERN_INFO "ecryptfs_write() at %08x%08x\n", temp.i[1], temp.i[0]);

	seller = kthread_run(cafe_machine, (void *)&current_cafe,  "CAFE_MACHINE");
	buyer = kthread_run(cafe_taster, (void *)&current_cafe,  "CAFE_TESTER");

	return 0;
}

static void __exit caferace_exit(void)
{
	kthread_stop(seller);
	kthread_stop(buyer);
}

module_init(caferace_init);
module_exit(caferace_exit);

MODULE_LICENSE("GPL");


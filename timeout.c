#include <linux/module.h> //Needed by all modules
#include <linux/kernel.h> //Needed for KERN_ALERT

static int __init hello_init(void)
{
    int timeout;
    wait_queue_head_t timeout_wq;
    init_waitqueue_head(&timeout_wq);
    printk("<1>Waiting for one second\n");
    printk("HZ = %d\n", HZ);
    sleep_on_timeout(&timeout_wq, HZ);                       //(1)
    timeout = interruptible_sleep_on_timeout(&timeout_wq, HZ);//(2)
    printk("timeout = %d\n", timeout);
    if (!timeout)
        printk("timeout\n");

    printk("<1>Hello world.\n");
    return 0;
}

static void __exit hello_exit(void)
{
    printk(KERN_ALERT "Goodbye world 1.\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zengxiaolong ");
MODULE_DESCRIPTION("A sample driver");
MODULE_SUPPORTED_DEVICE("testdevice");

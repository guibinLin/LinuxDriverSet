#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/sched.h>

#include <asm/uaccess.h>

#define KETPAD_NUM		4
#define KEY_UP			103
#define KEY_LEFT		105
#define KEY_RIGHT		106
#define KEY_DOWN		108

#define DEBUG_LOG		0

#if DEBUG_LOG
	#define DEBUG_PRINTK printk
#else
	#define DEBUG_PRINTK(x...)
#endif


/*
	keypad_interrupt: keypad {
		compatible = "tiny4412,keypad_interrupt";
		tiny4412,keypad1 = <&gpx3 2 GPIO_ACTIVE_HIGH>;
		tiny4412,keypad2 = <&gpx3 3 GPIO_ACTIVE_HIGH>;
		tiny4412,keypad3 = <&gpx3 4 GPIO_ACTIVE_HIGH>;
		tiny4412,keypad4 = <&gpx3 5 GPIO_ACTIVE_HIGH>;
	};
*/

struct keypad_des_t {
	char name[20];
	unsigned int gpio;
	unsigned int irq;
	unsigned int flag;  //irqflag
	unsigned int code;
};

static struct keypad_des_t keypad_des[] = {
	[0] = {
		.name = "tiny4412,keypad1",
		.flag = IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
		.code  = KEY_UP,
	},
	[1] = {
		.name = "tiny4412,keypad2",
		.flag = IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
		.code  = KEY_DOWN,
	},
	[2] = {
		.name = "tiny4412,keypad3",
		.flag = IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
		.code  = KEY_LEFT,
	},
	[3] = {
		.name = "tiny4412,keypad4",
		.flag = IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
		.code  = KEY_RIGHT,
	},
};

struct keypad_event {
	unsigned int code;
	unsigned int value;
};

struct keypad_dev_t {
	dev_t dev;
	struct cdev *cdev;
	struct class *cls;
	struct device *device;
	
	wait_queue_head_t wq_head;
	struct keypad_event event;
	int have_data; //1-hava data, 0-no data
	struct timer_list timer;
};

static struct keypad_dev_t *keypad_dev;
static struct keypad_des_t *pkeypad_des;


static void keypad_timer(unsigned long data)
{
	if(1 == gpio_get_value(pkeypad_des->gpio))
	{
		/* release */
		DEBUG_PRINTK("keypad : %s release\n",pkeypad_des->name);
		keypad_dev->event.code 	= pkeypad_des->code;
		keypad_dev->event.value = 0;
	}
	else
	{
		/* press */
		DEBUG_PRINTK("keypad : %s press\n",pkeypad_des->name);
		keypad_dev->event.code 	= pkeypad_des->code;
		keypad_dev->event.value = 1;
	}

	keypad_dev->have_data = 1;
	wake_up_interruptible(&keypad_dev->wq_head);
	
}


static irqreturn_t keypad_dev_irq(int irq, void *dev_id)
{
	pkeypad_des = (struct keypad_des_t *)dev_id;

	mod_timer(&keypad_dev->timer, jiffies + HZ / 100);
	
	return IRQ_HANDLED;
}

static int keypad_open (struct inode *inode, struct file *filp)
{
	DEBUG_PRINTK("------- %s -------\n",__func__);

	return 0;
}

static ssize_t keypad_read (struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	int ret;
	
	DEBUG_PRINTK("------- %s -------\n",__func__);

	wait_event_interruptible(keypad_dev->wq_head, keypad_dev->have_data);
	
	ret = copy_to_user(buf, &keypad_dev->event, count);
	if(0 != ret)
	{
		DEBUG_PRINTK("keypad_read : copy_to_user failed!\n");
		return -EIO;
	}
	
	memset(&keypad_dev->event, 0, sizeof(struct keypad_event));
	keypad_dev->have_data = 0;
	
	return count;
}

static int keypad_release (struct inode *inode, struct file *filp)
{
	DEBUG_PRINTK("------- %s -------\n",__func__);

	
	return 0;
}

static const struct file_operations keypad_fops = {
	.owner		= THIS_MODULE,
	.open		= keypad_open,
	.release 	= keypad_release,
	.read 		= keypad_read,
};

static int keypad_drv_probe(struct platform_device *pdev)
{
	int i,ret;
	
	DEBUG_PRINTK("------- %s -------\n",__func__);

	
	for(i = 0; i < KETPAD_NUM; i++)
	{
		keypad_des[i].gpio = of_get_named_gpio(pdev->dev.of_node, keypad_des[i].name, 0);
		keypad_des[i].irq  = gpio_to_irq(keypad_des[i].gpio);
	}


	keypad_dev = kzalloc(sizeof(struct keypad_dev_t), GFP_KERNEL);
	if(IS_ERR(keypad_dev))
	{
		DEBUG_PRINTK("kzalloc failed!\n");
		return -ENOMEM;
	}
	
	if(0 != alloc_chrdev_region(&keypad_dev->dev, 0, 1,"keypad_dev"))
	{
		DEBUG_PRINTK("alloc_chrdev_region failed!\n");
		goto err_kfree;
	}

	keypad_dev->cdev = cdev_alloc();
	if(IS_ERR(keypad_dev->cdev))
	{
		DEBUG_PRINTK("cdev_alloc failed!\n");
		goto err_free_chrdev;
	}
	cdev_init(keypad_dev->cdev, &keypad_fops);
	if(0 != cdev_add(keypad_dev->cdev, keypad_dev->dev, 1))
	{
		DEBUG_PRINTK("cdev_add failed!\n");
		goto err_free_cdev;
	}

	keypad_dev->cls = class_create(THIS_MODULE,"keypad");
	if(IS_ERR(keypad_dev->cls))
	{
		DEBUG_PRINTK("class_create failed!\n");
		goto err_free_cdev;
	}
	
	keypad_dev->device = device_create(keypad_dev->cls, NULL, keypad_dev->dev, NULL, "keypad");
	if(IS_ERR(keypad_dev->device))
	{
		DEBUG_PRINTK("device_create failed!\n");
		goto err_class;
	}

	for(i = 0; i < KETPAD_NUM; i++)
	{
		ret = request_irq(keypad_des[i].irq, keypad_dev_irq, keypad_des[i].flag, keypad_des[i].name, (void *)&keypad_des[i]);
	}

	init_waitqueue_head(&keypad_dev->wq_head);
	keypad_dev->have_data = 0;

	init_timer(&keypad_dev->timer);
	keypad_dev->timer.function = keypad_timer;
	add_timer(&keypad_dev->timer);
	
	return 0;

err_class:
	class_destroy(keypad_dev->cls);
err_free_cdev:
	cdev_del(keypad_dev->cdev);
err_free_chrdev:
	unregister_chrdev_region(keypad_dev->dev, 1);
err_kfree:
	kfree(keypad_dev);

	return -EBUSY;

}


static int keypad_drv_remove(struct platform_device *pdev)
{
	int i;
	DEBUG_PRINTK("------- %s -------\n",__func__);

	del_timer(&keypad_dev->timer);
	
	for(i = 0; i < KETPAD_NUM; i++)
	{
		free_irq(keypad_des[i].irq, (void *)&keypad_des[i]);
	}
	
	device_destroy(keypad_dev->cls, keypad_dev->dev);
	class_destroy(keypad_dev->cls);
	cdev_del(keypad_dev->cdev);
	unregister_chrdev_region(keypad_dev->dev, 1);
	kfree(keypad_dev);
	
	return 0;
}



const struct of_device_id keypad_match_table[] = {
		{.compatible = "tiny4412,keypad_interrupt",},
		{ },
};


static struct platform_driver keypad_pdrv = {
	.probe	= keypad_drv_probe,
	.remove = keypad_drv_remove,
	.driver = {
		.name 			= "keypad_pdrv",
		.of_match_table = keypad_match_table,
	},
};



static int __init keypad_drv_init(void)
{
	DEBUG_PRINTK("------- %s -------\n",__func__);
	return platform_driver_register(&keypad_pdrv);
}

static void __exit keypad_drv_exit(void)
{
	DEBUG_PRINTK("------- %s -------\n",__func__);
	platform_driver_unregister(&keypad_pdrv);
}

module_init(keypad_drv_init);
module_exit(keypad_drv_exit);
MODULE_LICENSE("GPL");





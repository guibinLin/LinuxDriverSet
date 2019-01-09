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

#include <asm/uaccess.h>


#define LED_ALL_ON	_IO('L', 0x1234)
#define LED_ALL_OFF	_IO('L', 0x1235)
#define LED_NUM_ON  _IOW('L', 0x1236, int)
#define LED_NUM_OFF _IOW('L', 0x1237, int)


/*
	led_pin {
	        compatible = "tiny4412,led_demo";
	        pinctrl-names = "led_demo";
	        pinctrl-0 = <&led_demo>;
	        tiny4412,led_gpio1 = <&gpm4 0 GPIO_ACTIVE_HIGH>;
	        tiny4412,led_gpio2 = <&gpm4 1 GPIO_ACTIVE_HIGH>;
	        tiny4412,led_gpio3 = <&gpm4 2 GPIO_ACTIVE_HIGH>;
	        tiny4412,led_gpio4 = <&gpm4 3 GPIO_ACTIVE_HIGH>;
	    };
*/

static struct pinctrl *pCtrl;
static struct pinctrl_state *pState;
static int ledPin1, ledPin2, ledPin3, ledPin4;

struct led_drv_t {
	dev_t devid;
	struct cdev *cdev;
	struct class *cls;
	struct device *dev;
};

static struct led_drv_t *led_drv;


static int led_open(struct inode *inode, struct file *filp)
{
	printk("------- %s -------\n",__func__);

	return 0;
}

static int led_release(struct inode *inode, struct file *filp)
{
	printk("------- %s -------\n",__func__);

	return 0;
}

static long led_ioctl(struct file *filp, unsigned int cmd, unsigned long args)
{
	printk("------- %s -------\n",__func__);

	switch(cmd)
	{
		case LED_NUM_ON:
			switch(args)
			{
				case 1: gpio_set_value(ledPin1, 0); break;
 				case 2: gpio_set_value(ledPin2, 0); break;
				case 3: gpio_set_value(ledPin3, 0); break;
				case 4: gpio_set_value(ledPin4, 0); break;
				default :
					break;
			}
		break;
		case LED_NUM_OFF:
			switch(args)
			{
				case 1: gpio_set_value(ledPin1, 1); break;
 				case 2: gpio_set_value(ledPin2, 1); break;
				case 3: gpio_set_value(ledPin3, 1); break;
				case 4: gpio_set_value(ledPin4, 1); break;
				default :
					break;
			}
		break;
		case LED_ALL_ON:
		{
			gpio_set_value(ledPin1, 0);
			gpio_set_value(ledPin2, 0);
			gpio_set_value(ledPin3, 0);
			gpio_set_value(ledPin4, 0);
			break;
		}
		
		case LED_ALL_OFF:
		{
			gpio_set_value(ledPin1, 1);
			gpio_set_value(ledPin2, 1);
			gpio_set_value(ledPin3, 1);
			gpio_set_value(ledPin4, 1);
			break;
		}

		default :
			break;
	}

	return 0;
}

const struct file_operations led_fops = {
	.open 			= led_open,
	.unlocked_ioctl = led_ioctl,
	.release 		= led_release,
}; 


static int led_drv_probe(struct platform_device *pdev)
{
	printk("------- %s -------\n",__func__);

	/* 获得pinctrl句柄 */
	pCtrl = devm_pinctrl_get(&pdev->dev);
	if(IS_ERR(pCtrl))
	{
		printk("devm_pinctrl_get error!\n");
		return -EBUSY;
	}

	/* pinctrl对应的state */
	pState = pinctrl_lookup_state(pCtrl, "led_demo");
	if(IS_ERR(pState))
	{
		printk("pinctrl_lookup_state error!\n");
		goto err_pinctrl;
	}

	/* 将GPIO口设置为输出模式 */
	if(0 != pinctrl_select_state(pCtrl, pState))
	{
		printk("pinctrl_select_state error!\n");
		goto err_pinctrl;
	}

	/* 通过设备节点名称获得对应的gpio口 */
	ledPin1 = of_get_named_gpio(pdev->dev.of_node, "tiny4412,led_gpio1", 0);
	ledPin2 = of_get_named_gpio(pdev->dev.of_node, "tiny4412,led_gpio2", 0);
	ledPin3 = of_get_named_gpio(pdev->dev.of_node, "tiny4412,led_gpio3", 0);
	ledPin4 = of_get_named_gpio(pdev->dev.of_node, "tiny4412,led_gpio4", 0);
	if((ledPin1 < 0) || (ledPin2 < 0) || (ledPin3 < 0) || (ledPin4 < 0)) 
	{
		printk("of_get_named_gpio error!\n");
		goto err_pinctrl;
	} 
	else 
	{
		if(devm_gpio_request_one(&pdev->dev, ledPin1, GPIOF_OUT_INIT_HIGH, "led1") < 0)
		{
			printk("devm_gpio_request_one ledPin1 error!\n");
			goto err_pinctrl;
		}
		if(devm_gpio_request_one(&pdev->dev, ledPin2, GPIOF_OUT_INIT_HIGH, "led2") < 0)
		{
			printk("devm_gpio_request_one ledPin2 error!\n");
			goto err_free_pin1;
		}
		if(devm_gpio_request_one(&pdev->dev, ledPin3, GPIOF_OUT_INIT_HIGH, "led3") < 0)
		{
			printk("devm_gpio_request_one ledPin3 error!\n");
			goto err_free_pin2;
		}
		if(devm_gpio_request_one(&pdev->dev, ledPin4, GPIOF_OUT_INIT_HIGH, "led4") < 0)
		{
			printk("devm_gpio_request_one ledPin4 error!\n");
			goto err_free_pin3;
		}
	}

	/* 分配空间 */
	led_drv = kzalloc(sizeof(struct led_drv_t), GFP_KERNEL);
	if(IS_ERR(led_drv))
	{
		printk("kzalloc error!\n");
		goto err_free_pin4;
	}

	/* 申请设备号 */
	if(0 != alloc_chrdev_region(&led_drv->devid, 0, 1, "led_demo"))
	{
		printk("alloc_chrdev_region error!\n");
		goto err_alloc;
	}

	/* 分配/设置/注册 cdev       */
	led_drv->cdev = cdev_alloc();
	if(IS_ERR(led_drv->cdev))
	{
		printk("cdev_alloc error!\n");
		goto err_chrdev;
	}
	cdev_init(led_drv->cdev, &led_fops);
	if(0 != cdev_add(led_drv->cdev, led_drv->devid, 1))
	{
		printk("cdev_add error!\n");
		goto err_cdev;
	}

	/* 创建类 */
	led_drv->cls = class_create(THIS_MODULE, "led_demo");
	if(IS_ERR(led_drv->cls))
	{
		printk("class_create error!\n");
		goto err_cdev;
	}

	/* 创建设备节点 */
	led_drv->dev = device_create(led_drv->cls, NULL, led_drv->devid, NULL, "LedNode");
	if(IS_ERR(led_drv->dev))
	{
		printk("device_create error!\n");
		goto err_cls;
	}

	return 0;
	
err_cls:
	class_destroy(led_drv->cls);
err_cdev:
	cdev_del(led_drv->cdev);
err_chrdev:
	unregister_chrdev_region(led_drv->devid, 1);
err_alloc:
	kfree(led_drv);
err_free_pin4:
	devm_gpio_free(&pdev->dev, ledPin4);
err_free_pin3:
	devm_gpio_free(&pdev->dev, ledPin3);
err_free_pin2:
	devm_gpio_free(&pdev->dev, ledPin2);
err_free_pin1:
	devm_gpio_free(&pdev->dev, ledPin1);
err_pinctrl:
	devm_pinctrl_put(pCtrl);
	
	return -EBUSY;
}


static int led_drv_remove(struct platform_device *pdev)
{
	printk("------- %s -------\n",__func__);

	device_destroy(led_drv->cls, led_drv->devid);
	class_destroy(led_drv->cls);
	cdev_del(led_drv->cdev);
	unregister_chrdev_region(led_drv->devid, 1);
	kfree(led_drv);
	
	devm_gpio_free(&pdev->dev, ledPin1);
	devm_gpio_free(&pdev->dev, ledPin2);
	devm_gpio_free(&pdev->dev, ledPin3);
	devm_gpio_free(&pdev->dev, ledPin4);

	devm_pinctrl_put(pCtrl);

	return 0;
}

const struct of_device_id led_match_table[] = {
		{.compatible = "tiny4412,led_demo",},
		{ },
};

static struct platform_driver led_pdrv = {
	.probe	= led_drv_probe,
	.remove = led_drv_remove,
	.driver = {
		.name 			= "led_pdrv",
		.of_match_table = led_match_table,
	},
};



static int __init led_drv_init(void)
{
	printk("------- %s -------\n",__func__);

	return platform_driver_register(&led_pdrv);
}

static void __exit led_drv_exit(void)
{
	printk("------- %s -------\n",__func__);

	platform_driver_unregister(&led_pdrv);
}

module_init(led_drv_init);
module_exit(led_drv_exit);
MODULE_LICENSE("GPL");




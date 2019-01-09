#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/sched.h>

#include <asm/uaccess.h>


#define DEBUG_LOG		1

#if DEBUG_LOG
	#define DEBUG_PRINTK printk
#else
	#define DEBUG_PRINTK(x...)
#endif

/*
	backlight@139D0000{
+               compatible = "tiny4412,backlight";
+               reg = <0x139D0000  0x14>;
+               tiny4412,backlight = <&gpx1 2 GPIO_ACTIVE_HIGH>;
+               pinctrl-names = "backlight_out","backlight_in";
+               pinctrl-0 = <&backlight_out>;
+               pinctrl-1 = <&backlight_in>;
+               interrupts = <0 40 0>;
+               clocks = <&clock CLK_PWM>;
+               clock-names = "timers";
+        };

+&pinctrl_1 {
+        backlight_out: backlight_out{
+                samsung,pins = "gpx1-2";
+                samsung,pin-function = <1>;
+                samsung,pin-pud = <0>;
+                samsung,pin-drv = <0>;
+        };
+         backlight_in: backlight_in{
+                samsung,pins = "gpx1-2";
+                samsung,pin-function = <0>;
+                samsung,pin-pud = <0>;
+                samsung,pin-drv = <0>;
+        };
+};

*/

struct TIMER_BASE
{
    unsigned int TCFG0;
    unsigned int TCFG1;
    unsigned int TCON;
    unsigned int TCNTB0;
    unsigned int TCMPB0;
    unsigned int TCNTO0;
    unsigned int TCNTB1;
    unsigned int TCMPB1;
    unsigned int TCNTO1;
    unsigned int TCNTB2;
    unsigned int TCMPB2;
    unsigned int TCNTO2;
    unsigned int TCNTB3;
    unsigned int TCMPB3;
    unsigned int TCNTO3;
    unsigned int TCNTB4;
    unsigned int TCBTO4;
    unsigned int TINT_CSTAT;
};
volatile static struct TIMER_BASE *timer_base = NULL;


static struct pinctrl *pCtrl;
static struct pinctrl_state *pBacklightInState, *pBacklightOutState;
static int one_write_pin;
static struct resource *mem_resource, *irq_resource;
static struct clk *timer_clk;

static dev_t devid;
static struct class *cls;
static struct device *dev;
static struct cdev *cdev;

static volatile unsigned int io_bit_count;
static volatile unsigned int io_data;

enum
{
    IDLE,
    START,
    REQUEST,
    WAITING,
    RESPONSE,
    STOPING,
} one_wire_status = IDLE;

static inline void stop_timer_for_1wire(void)
{
    unsigned long tcon;
    tcon = timer_base->TCON;
    tcon &= ~(1 << 16);
    timer_base->TCON = tcon;
}

static irqreturn_t timer_for_1wire_interrupt(int irq, void *dev_id)
{
    unsigned int tint;
    tint = timer_base->TINT_CSTAT;
    tint |= 0x100;
    timer_base->TINT_CSTAT = tint;
    //DEBUG_PRINTK("timer_for_1wire_interrupt\n");
    io_bit_count--;
    switch (one_wire_status)
    {
        case START:
            if (io_bit_count == 0)
            {
                io_bit_count = 16;
                one_wire_status = REQUEST;
            }
            break;
        case REQUEST:
            gpio_set_value(one_write_pin, io_data & (1U << 31));
            io_data <<= 1;
            if (io_bit_count == 0)
            {
                io_bit_count = 2;
                one_wire_status = WAITING;
            }
            break;
        case WAITING:
            if (io_bit_count == 0)
            {
                io_bit_count = 32;
                one_wire_status = RESPONSE;
            }
            if (io_bit_count == 1)
            {
                pinctrl_select_state(pCtrl, pBacklightInState);
                gpio_set_value(one_write_pin, 1);
            }
            break;
        case RESPONSE:
            io_data = (io_data << 1) | gpio_get_value(one_write_pin);
            if (io_bit_count == 0)
            {
                io_bit_count = 2;
                one_wire_status = STOPING;
                gpio_set_value(one_write_pin, 1);
                pinctrl_select_state(pCtrl, pBacklightOutState);
                //one_wire_session_complete(one_wire_request, io_data);
            }
            break;
        case STOPING:
            if (io_bit_count == 0)
            {
                one_wire_status = IDLE;
                stop_timer_for_1wire();
            }
            break;
        default:
            stop_timer_for_1wire();
    }
    return IRQ_HANDLED;
}


static const unsigned char crc8_tab[] =
{
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
    0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
    0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
    0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
    0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
    0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
    0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
    0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
    0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
    0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
    0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
    0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
    0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
    0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
    0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
    0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3,
};

#define crc8_init(crc) ((crc) = 0XACU)
#define crc8(crc, v) ( (crc) = crc8_tab[(crc) ^(v)])

static void start_one_wire_session(unsigned char req)
{
    unsigned int tcon;
    DEBUG_PRINTK("start_one_wire_session\n");
    one_wire_status = START;
    gpio_set_value(one_write_pin, 1);
    pinctrl_select_state(pCtrl, pBacklightOutState);
    // IDLE to START
    {
        unsigned char crc;
        crc8_init(crc);
        crc8(crc, req);
        io_data = (req << 8) + crc;
        io_data <<= 16;
    }
    io_bit_count = 1;
    pinctrl_select_state(pCtrl, pBacklightOutState);
    timer_base->TCNTB3 = 650;
    //init tranfer and start timer
    tcon = timer_base->TCON;
    tcon &= ~(0xF << 16);
    tcon |= (1 << 17);
    timer_base->TCON = tcon;
    tcon |= (1 << 16);
    tcon |= (1 << 19);
    tcon &= ~(1 << 17);
    timer_base->TCON = tcon;
    timer_base->TINT_CSTAT |= 0x08;
    gpio_set_value(one_write_pin, 0);
}


static int backlight_open(struct inode *inode, struct file *filp)
{
	DEBUG_PRINTK("------- %s -------\n",__func__);
    return 0;
}
static int backlight_release(struct inode *inode, struct file *filp)
{
	DEBUG_PRINTK("------- %s -------\n",__func__);
    return 0;
}

static ssize_t backlight_write(struct file *file, const char __user *buf, size_t count, loff_t *off)
{
    unsigned char reg, ret;

	DEBUG_PRINTK("------- %s -------\n",__func__);
	
    ret = copy_from_user(&reg, buf, 1);
  	if (ret < 0)
    {
        DEBUG_PRINTK("%s copy_from_user error\n", __func__);
    }
    
    //DEBUG_PRINTK("kernel: reg = %d", reg);
    //DEBUG_PRINTK("buf = %d", *buf);
    
    start_one_wire_session(reg);
	
    return 1;
}


static struct file_operations backlight_fops =
{
    .owner              = THIS_MODULE,
    .open               = backlight_open,
    .release            = backlight_release,
    .write              = backlight_write,
};


static int backlight_drv_probe(struct platform_device *pdev)
{
	int ret;
	
	DEBUG_PRINTK("------- %s -------\n",__func__);

	pCtrl = devm_pinctrl_get(&pdev->dev);
	if(IS_ERR(pCtrl))
	{
		DEBUG_PRINTK("devm_pinctrl_get error!\n");
		return -EBUSY;
	}

	pBacklightInState 	= pinctrl_lookup_state(pCtrl, "backlight_in");
	pBacklightOutState 	= pinctrl_lookup_state(pCtrl, "backlight_out");
	if(IS_ERR(pBacklightInState) || IS_ERR(pBacklightOutState))
	{
		DEBUG_PRINTK("pinctrl_lookup_state error!\n");
		return -EBUSY;
	}

	one_write_pin = of_get_named_gpio(pdev->dev.of_node, "tiny4412,backlight", 0);
	if(one_write_pin < 0)
	{
		DEBUG_PRINTK("of_get_named_gpio error!\n");
		goto err_pinctrl;
	}

	mem_resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq_resource = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if(IS_ERR(mem_resource) || IS_ERR(irq_resource))
	{
		DEBUG_PRINTK("platform_get_resource error!\n");
		return -EBUSY;
	}
	

	timer_clk = devm_clk_get(&pdev->dev, "timers");
	if(IS_ERR(timer_clk))
	{
		DEBUG_PRINTK("devm_clk_get error!\n");
		return -EBUSY;
	}

	if(clk_prepare_enable(timer_clk) < 0)
	{
		DEBUG_PRINTK("clk_prepare_enable error!\n");
		return -EBUSY;
	}


	timer_base = devm_ioremap_resource(&pdev->dev, mem_resource);
	timer_base->TCFG0  = 0xF00;
    timer_base->TCFG1  = 0x10004;

	ret = devm_request_irq(&pdev->dev, irq_resource->start, timer_for_1wire_interrupt, IRQF_TIMER, "backlight", NULL);
	if(ret)
	{
		DEBUG_PRINTK("devm_request_irq error!\n");
		return -EBUSY;
	}

	start_one_wire_session(0x60);

	/* 申请设备号 */
	if(0 != alloc_chrdev_region(&devid, 0, 1, "backlight"))
	{
		DEBUG_PRINTK("alloc_chrdev_region error!\n");
		return -EINVAL;
	}

	/* 分配/设置/注册 cdev */
	cdev = cdev_alloc();
	if(IS_ERR(cdev))
	{
		DEBUG_PRINTK("cdev_alloc error!\n");
		goto err_chrdev;
	}
	cdev_init(cdev, &backlight_fops);
	if(0 != cdev_add(cdev, devid, 1))
	{
		DEBUG_PRINTK("cdev_add error!\n");
		goto err_cdev;
	}

	/* 创建类 */
	cls = class_create(THIS_MODULE, "backlight_class");
	if(IS_ERR(cls))
	{
		DEBUG_PRINTK("class_create error!\n");
		goto err_cdev;
	}

	/* 创建设备节点 */
	dev = device_create(cls, NULL, devid, NULL, "backlight");
	if(IS_ERR(dev))
	{
		DEBUG_PRINTK("device_create error!\n");
		goto err_cls;
	}
	
	return 0;
	
err_cls:
	class_destroy(cls);
err_cdev:
	cdev_del(cdev);
err_chrdev:
	unregister_chrdev_region(devid, 1);
err_pinctrl:
	devm_pinctrl_put(pCtrl);


	return -EINVAL;

	
}

static int backlight_drv_remove(struct platform_device *pdev)
{
	DEBUG_PRINTK("------- %s -------\n",__func__);

	return 0;
}

const struct of_device_id backlight_match_table[] = {
		{.compatible = "tiny4412,backlight",},
		{ },
};

static struct platform_driver backlight_pdrv = {
	.probe	= backlight_drv_probe,
	.remove = backlight_drv_remove,
	.driver = {
		.name 			= "backlight_pdrv",
		.of_match_table = backlight_match_table,
	},
};

static int __init backlight_drv_init(void)
{
	DEBUG_PRINTK("------- %s -------\n",__func__);
	return platform_driver_register(&backlight_pdrv);
}

static void __exit backlight_drv_exit(void)
{
	DEBUG_PRINTK("------- %s -------\n",__func__);
	platform_driver_unregister(&backlight_pdrv);
}

module_init(backlight_drv_init);
module_exit(backlight_drv_exit);
MODULE_DESCRIPTION("Exynos4412 LCD Backlight Driver");
MODULE_AUTHOR("KiFF <guibin_lin@163.com>");
MODULE_VERSION("V1.0");
MODULE_LICENSE("GPL");





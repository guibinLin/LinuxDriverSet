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
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/input.h>

#include <asm/uaccess.h>

#define DEBUG_LOG		0

#if DEBUG_LOG
	#define DEBUG_PRINTK printk
#else
	#define DEBUG_PRINTK(x...)
#endif

#define X_MAX 800
#define Y_MAX 480
#define ID_MAX 5


/*
	设备树参考：
		linux-4.4.1/Documentation/devicetree/bindings/input/touchscreen/focaltech-ft6236.txt

	tp_ft5x06@38 {
        compatible = "tiny4412,ft5x06";
        interrupts = <6 0>;
        interrupt-parent = <&gpx1>;
        status = "okay";
    };

	&i2c_1 {
    status = "okay";

    ft5406@38 {
        compatible = "tiny4412,ft5406";
        reg = <0x38>;
    };
};


*/

struct ts_event {
	int x;
	int y;
	int id;
};

struct ts_dev_t{
	struct i2c_client *client;
	struct work_struct ts_work;
	struct ts_event ts_events[5];
};

static struct input_dev *ft5x06_dev;
static struct ts_dev_t *ts_dev;
static unsigned int ts_irq;

#if 0

static int ft5x06_i2c_write_data(struct i2c_client * client, char *txbuf, int count)
{
	
	struct i2c_msg msgs;
	
	msgs.addr 	= client->addr;
	msgs.flags 	= 0;
	msgs.buf 	= txbuf;
	msgs.len 	= count; 

	if(1 != i2c_transfer(client->adapter, &msgs, 1))
	{
		DEBUG_PRINTK("i2c_transfer error!\n");
		return -EINVAL;
	}

	return count;
}

#endif

static int ft5x06_i2c_read_data(struct i2c_client * client, char *rxbuf, int count)
{
	
	struct i2c_msg msgs[] = {
		{
			.addr 	= client->addr,
			.flags 	= 0, //write
			.len 	= 1,
			.buf 	= rxbuf,
		},
		{
			.addr	= client->addr,
			.flags	= I2C_M_RD, //read
			.len 	= count,
			.buf 	= rxbuf,
		},
	};

	if(2 != i2c_transfer(client->adapter, msgs, 2))
	{
		DEBUG_PRINTK("i2c_transfer error!\n");
		return -EINVAL;
	}

	return count;
}


static int ft5x06_read_vendor_id(struct i2c_client * client)
{
	int ret;
	u8 buf[1] = {0};

	buf[0] = 0xa3; //chip vendor id
	
	ret = ft5x06_i2c_read_data(client, buf, 1);
	if(ret < 0)
	{
		DEBUG_PRINTK("ft5x06_read_vendor_id fail!\n");
		return ret;
	}

	DEBUG_PRINTK("chip vendor id : 0x%x!\n",buf[0]);

	if(0x55 != buf[0])
	{
		DEBUG_PRINTK("chip vendor id read error!\n");
		return ret;
	}

	return 0;
}

static void ft5x06_work_func(struct work_struct *work)
{
	u8 buf[32] = {0};
	int ret, i, pointCnt = 0;

	/* 判断触点个数 */
	ret = ft5x06_i2c_read_data(ts_dev->client, buf, 32);
	if(ret < 0)
	{
		DEBUG_PRINTK("%s : read pointCnt error!\n", __func__);
		return;
	}

	pointCnt = buf[2] & 0xf;

	/* 获取触点数据 */
	switch(pointCnt)
	{
		case 5:
			ts_dev->ts_events[4].x  = (s16)(buf[27] & 0xf) << 8 | buf[28];
			ts_dev->ts_events[4].y  = (s16)(buf[29] & 0xf) << 8 | buf[30];
			ts_dev->ts_events[4].id = (buf[29] >> 4); 
		case 4:
			ts_dev->ts_events[3].x  = (s16)(buf[21] & 0xf) << 8 | buf[22];
			ts_dev->ts_events[3].y  = (s16)(buf[23] & 0xf) << 8 | buf[24];
			ts_dev->ts_events[3].id = (buf[23] >> 4); 
		case 3:
			ts_dev->ts_events[2].x  = (s16)(buf[15] & 0xf) << 8 | buf[16];
			ts_dev->ts_events[2].y  = (s16)(buf[17] & 0xf) << 8 | buf[18];
			ts_dev->ts_events[2].id = (buf[17] >> 4); 
		case 2:
			ts_dev->ts_events[1].x  = (s16)(buf[9] & 0xf) << 8 | buf[10];
			ts_dev->ts_events[1].y  = (s16)(buf[11] & 0xf) << 8 | buf[12];
			ts_dev->ts_events[1].id = (buf[11] >> 4); 
		case 1:
			ts_dev->ts_events[0].x  = (s16)(buf[3] & 0xf) << 8 | buf[4];
			ts_dev->ts_events[0].y  = (s16)(buf[5] & 0xf) << 8 | buf[6];
			ts_dev->ts_events[0].id = (buf[5] >> 4); 
			break;
		case 0:
			break;
		default:
			break;
	}

	/* 无触点，上报数据 */
	if(!pointCnt)
	{
		input_mt_sync(ft5x06_dev);
		input_sync(ft5x06_dev);
		return;
	}

	/* 有触点，上报数据 */
	for(i = 0; i < pointCnt; i++)
	{
		input_report_abs(ft5x06_dev, ABS_MT_POSITION_X, ts_dev->ts_events[i].x);
		input_report_abs(ft5x06_dev, ABS_MT_POSITION_Y, ts_dev->ts_events[i].y);
		input_report_abs(ft5x06_dev, ABS_MT_TRACKING_ID, ts_dev->ts_events[i].id);
		input_mt_sync(ft5x06_dev);
	}

	input_sync(ft5x06_dev);
	
}


static irqreturn_t ft5x06_interrupt(int irq, void *dev_id)
{
	DEBUG_PRINTK("------- %s -------\n",__func__);
	
	schedule_work(&ts_dev->ts_work);

	return IRQ_HANDLED;
}

static int ft5x06_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;

	DEBUG_PRINTK("------- %s -------\n",__func__);

	ts_dev = kzalloc(sizeof(struct ts_dev_t), GFP_KERNEL);
	if(NULL == ts_dev)
	{
		DEBUG_PRINTK("kzalloc error!\n");
		return -ENOMEM;
	}
	
	ts_dev->client = client;

	/* 0.检查I2C设备是否存在，读取chip vendor ID */
	if(0 != ft5x06_read_vendor_id(ts_dev->client))
	{
		DEBUG_PRINTK("ft5x06_read_vendor_id error!\n");
		return -EINVAL;
	}

	/* 1.分配input_device结构体 */
	ft5x06_dev = input_allocate_device();
	if(NULL == ft5x06_dev)
	{
		DEBUG_PRINTK("input_allocate_device error!\n");
		return -EINVAL;
	}

	/* 2.设置input_device */
	ft5x06_dev->name = "ft5x06";
	ft5x06_dev->id.bustype = BUS_I2C;
	ft5x06_dev->id.product = 0x1;
	ft5x06_dev->id.vendor = 0x1;
	ft5x06_dev->id.version = 0x1;
	
	/* 2.1 设置可以产生哪类事件 */
	set_bit(EV_SYN, ft5x06_dev->evbit);
	set_bit(EV_ABS, ft5x06_dev->evbit);

	/* 2.2 设置可以产生哪种事件 */
	set_bit(ABS_MT_POSITION_X, ft5x06_dev->absbit);
	set_bit(ABS_MT_POSITION_Y, ft5x06_dev->absbit);
	set_bit(ABS_MT_TRACKING_ID, ft5x06_dev->absbit);

	/* 2.3 设置产生事件的范围 */
	input_set_abs_params(ft5x06_dev, ABS_MT_POSITION_X, 0, X_MAX, 0, 0);	
	input_set_abs_params(ft5x06_dev, ABS_MT_POSITION_Y, 0, Y_MAX, 0, 0);	
	input_set_abs_params(ft5x06_dev, ABS_MT_TRACKING_ID, 0, ID_MAX, 0, 0);

	/* 3.硬件操作 */
	/* 3.1 初始化工作队列 */
	INIT_WORK(&ts_dev->ts_work, ft5x06_work_func);

	/* 3.2 申请中断 */
	ret = devm_request_threaded_irq(&client->dev, ts_irq, ft5x06_interrupt, NULL, IRQF_TRIGGER_FALLING|IRQF_ONESHOT, client->name, (void *)ts_dev);
	if (ret) 
	{
		DEBUG_PRINTK("request irq %d failed: %d\n", ts_irq, ret);
		return -EINVAL;
	}

	/* 4.注册input_device */
	if(0 != input_register_device(ft5x06_dev))
	{
		DEBUG_PRINTK("input_register_device error!\n");
		return -EINVAL;
	}
	
	return 0;
}


static int ft5x06_i2c_remove(struct i2c_client *client)
{
	input_unregister_device(ft5x06_dev);
	devm_free_irq(&ts_dev->client->dev, ts_dev->client->irq, (void *)ts_dev);
	input_free_device(ft5x06_dev);
	kfree(ts_dev);
	
	return 0;
}


static const struct i2c_device_id ft5x06_i2c_id_table[] = {
	{ "ft5406", 0 },
	{ }
};

static struct i2c_driver ft5x06_i2c_driver = {
	.driver = {
		.name	= "ft5406",
	},
	.probe		= ft5x06_i2c_probe,
	.remove		= ft5x06_i2c_remove,
	.id_table	= ft5x06_i2c_id_table,
};

static int touchscreen_drv_probe(struct platform_device *pdev)
{
	DEBUG_PRINTK("------- %s -------\n",__func__);
	ts_irq = platform_get_irq(pdev, 0);
	
	return i2c_add_driver(&ft5x06_i2c_driver);
}

static int touchscreen_drv_remove(struct platform_device *pdev)
{
	DEBUG_PRINTK("------- %s -------\n",__func__);

	i2c_del_driver(&ft5x06_i2c_driver);

	return 0;
}


const struct of_device_id touchscreen_match_table[] = {
		{.compatible = "tiny4412,ft5x06",},
		{ },
};


static struct platform_driver touchscreen_pdrv = {
	.probe	= touchscreen_drv_probe,
	.remove = touchscreen_drv_remove,
	.driver = {
		.name 			= "touchscreen_pdrv",
		.of_match_table = touchscreen_match_table,
	},
};

static int __init touchscreen_drv_init(void)
{
	DEBUG_PRINTK("------- %s -------\n",__func__);
	return platform_driver_register(&touchscreen_pdrv);
}

static void __exit touchscreen_drv_exit(void)
{
	DEBUG_PRINTK("------- %s -------\n",__func__);
	platform_driver_unregister(&touchscreen_pdrv);
}

module_init(touchscreen_drv_init);
module_exit(touchscreen_drv_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("KiFF <guibin_lin@163.com>");
MODULE_DESCRIPTION("Exynos4412 TouchScreen driver.");
MODULE_VERSION("V1.0");




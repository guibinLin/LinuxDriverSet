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
#include <linux/ioport.h>
#include <linux/dma-mapping.h>

#include <asm/uaccess.h>
#include <asm-generic/io.h>

#define DEBUG_LOG		1

#if DEBUG_LOG
	#define DEBUG_PRINTK printk
#else
	#define DEBUG_PRINTK(x...)
#endif


#define LCM_WIDTH	800
#define LCM_HEIGH	480
#define LCM_BPP		32

#define VIDCON0			0x00
#define VIDCON1         0x04
#define VIDTCON0        0x10
#define VIDTCON1        0x14
#define VIDTCON2        0x18
#define WINCON0         0x20
#define SHADOWCON       0x34
#define WINCHMAP2       0x3c
#define VIDOSD0A        0x40
#define VIDOSD0B        0x44
#define VIDOSD0C        0x48
#define VIDW00ADD0B0    0xA0
#define VIDW00ADD1B0    0xD0


#define CLK_SRC_LCD0 		0x234 //Selects clock source for LCD_BLK
#define CLK_SRC_MASK_LCD	0x334 //Clock source mask for LCD_BLK
#define CLK_DIV_LCD			0x534 //Sets clock divider ratio for LCD_BLK
#define CLK_DIV_STAT_LCD 	0x634 //Clock divider status for LCD_BLK
#define CLK_GATE_IP_LCD 	0x934 //Controls IP clock gating for LCD_BLK
#define LCDBLK_CFG 			0x00 //Display control register
#define LCDBLK_CFG2 		0x04 //Display control register


/*
	lcd_s702@11c00000 {
        compatible = "tiny4412,lcd_s702";
        reg = <0x11C00000  0x20c0 0x10010210 0x08 0x10023c80 0x04 0x1003c000 0x1000>;
        pinctrl-names = "default";
        pinctrl-0 = <&lcd_s702>;
        clocks = <&clock CLK_FIMD0 &clock CLK_ACLK160>;
        clock-names = "fimd0","aclk160";
    };

    0x11C00000 : LCM寄存器基地址
    0x10010210 : LCDBLK_CFG -- Display control register
    0x10023c80 : LCD0_CONFIGURATION --  Configures power mode of LCD0
	0x1003c000 : 
*/

static struct fb_info *tiny4412_fb;
static u32 pseudo_palette[16];
static struct resource *mem_resource_lcm_base, *mem_resource_lcm_ctrl, *mem_resource_lcm_power, *mem_resource_lcm_clock;
static void __iomem *lcm_base, *lcm_ctrl, *lcm_power, *lcm_clock;

static inline unsigned int chan_to_field(unsigned int chan, struct fb_bitfield *bf)
{
    chan &= 0xFFFF;
    chan >>= 16 - bf->length;
    return chan << bf->offset;
}

static int cfb_setcolreg(unsigned int regno, unsigned int red,
                               unsigned int green, unsigned int blue,
                               unsigned int transp, struct fb_info *info)
{
    unsigned int color = 0;
    uint32_t *p;
    color  = chan_to_field(red,   &info->var.red);
    color |= chan_to_field(green, &info->var.green);
    color |= chan_to_field(blue,  &info->var.blue);
    
    p = info->pseudo_palette;  
    p[regno] = color;
    return 0;
}

static struct fb_ops tiny4412_fbops = {
	.owner              = THIS_MODULE,
    .fb_setcolreg       = cfb_setcolreg, //设置RGB颜色，实现伪颜色表 
    .fb_fillrect        = cfb_fillrect,  //矩形填充
    .fb_copyarea        = cfb_copyarea,  //数据复制
    .fb_imageblit       = cfb_imageblit, //图形填充
};

static int lcm_drv_probe(struct platform_device *pdev)
{
	unsigned int temp;

	DEBUG_PRINTK("------- %s -------\n",__func__);

	/* 1.分配fb_info结构体 */
	tiny4412_fb = framebuffer_alloc(0, NULL);
	if(IS_ERR(tiny4412_fb))
	{
		DEBUG_PRINTK("framebuffer_alloc error!\n");
		return -EINVAL;
	}

	/* 2.设置fb_info结构体 */
	/* 2.1 设置固定参数 */
	strcpy(tiny4412_fb->fix.id, "lcd_s702");
	tiny4412_fb->fix.smem_len 	= LCM_WIDTH * LCM_HEIGH * LCM_BPP / 8;	// 显存物理地址(长度) 
	//tiny4412_fb->fix.smem_start = ;	// 显存物理地址(起始) 
	tiny4412_fb->fix.type 		= FB_TYPE_PACKED_PIXELS;
	tiny4412_fb->fix.visual 	= FB_VISUAL_TRUECOLOR;
	tiny4412_fb->fix.line_length = LCM_WIDTH * LCM_BPP / 8;
	
	
	/* 2.2 设置可变参数 */
	tiny4412_fb->var.xres 			= LCM_WIDTH;
	tiny4412_fb->var.yres 			= LCM_HEIGH;
	tiny4412_fb->var.xres_virtual 	= LCM_WIDTH;
	tiny4412_fb->var.yres_virtual 	= LCM_HEIGH;
	tiny4412_fb->var.xoffset 		= 0;
	tiny4412_fb->var.yoffset 		= 0;
	tiny4412_fb->var.bits_per_pixel = LCM_BPP;

	/* RGB:888 */
	tiny4412_fb->var.red.offset 	= 16;
	tiny4412_fb->var.red.length 	= 8;
	tiny4412_fb->var.green.offset 	= 8;
	tiny4412_fb->var.green.length 	= 8;
	tiny4412_fb->var.blue.offset 	= 0;
	tiny4412_fb->var.blue.length 	= 8;
	tiny4412_fb->var.activate 		= FB_ACTIVATE_NOW;

	/* 2.3 设置其它参数 */
	tiny4412_fb->fbops = &tiny4412_fbops;							//LCM操作函数
	//tiny4412_fb->screen_base = ; 									//显存虚拟地址(起始) 
	tiny4412_fb->screen_size = LCM_WIDTH * LCM_HEIGH * LCM_BPP / 8;	//显存虚拟地址(长度) 
	tiny4412_fb->pseudo_palette = pseudo_palette;					//调色板

	
	/* 3.硬件操作 */
	/* 3.1 配置GPIO用于LCD */
	//在设备树中，将 GPF0_0-GPF0_7、GPF1_0-GPF1_7、GPF2_0-GPF2_7、GPF3_0-GPF3_3
	//配置为了复用第二功能(LCD)，禁止内部上拉，驱动强度配置设置为0;

	/* 3.2 寄存器地址映射 */
	mem_resource_lcm_base 	= platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mem_resource_lcm_ctrl 	= platform_get_resource(pdev, IORESOURCE_MEM, 1);
	mem_resource_lcm_power 	= platform_get_resource(pdev, IORESOURCE_MEM, 2);
	mem_resource_lcm_clock	= platform_get_resource(pdev, IORESOURCE_MEM, 3);
	if((NULL == mem_resource_lcm_base) || (NULL == mem_resource_lcm_ctrl) || (NULL == mem_resource_lcm_power) || (NULL == mem_resource_lcm_clock))
	{
		DEBUG_PRINTK("platform_get_resource error!\n");
		return -EINVAL;
	}
	
	lcm_base = devm_ioremap(&pdev->dev, mem_resource_lcm_base->start, resource_size(mem_resource_lcm_base));
	lcm_ctrl = devm_ioremap(&pdev->dev, mem_resource_lcm_ctrl->start, resource_size(mem_resource_lcm_ctrl));
	lcm_power = devm_ioremap(&pdev->dev, mem_resource_lcm_power->start, resource_size(mem_resource_lcm_power));
	lcm_clock = devm_ioremap(&pdev->dev, mem_resource_lcm_clock->start, resource_size(mem_resource_lcm_clock));
	if((NULL == lcm_base) || (NULL == lcm_ctrl) || (NULL == lcm_power) || (NULL == lcm_clock))
	{
		DEBUG_PRINTK("devm_ioremap error!\n");
		return -EINVAL;
	}

	*(unsigned long *)lcm_power = 7; //Reset Value = 0x00000007
	
	/* 3.3 配置时钟 */
	//时钟源选择\使能时钟
    //Selects clock source for LCD_BLK
    //FIMD0_SEL:bit[3:0]=0110=SCLKMPLL_USER_T=800M
    temp = readl(lcm_clock + CLK_SRC_LCD0);
	temp &= ~(0xf);	
	temp |= 0x6;
	writel(temp, lcm_clock + CLK_SRC_LCD0);
	
	//Clock source mask for LCD_BLK    
	//FIMD0_MASK:Mask output clock of MUXFIMD0 (1=Unmask)
	temp = readl(lcm_clock + CLK_SRC_MASK_LCD);
	temp |= 0x1;
	writel(temp, lcm_clock + CLK_SRC_MASK_LCD);

	//Clock source mask for LCD_BLK    
	//SCLK_FIMD0 = MOUTFIMD0/(FIMD0_RATIO + 1),分频比 1/1
	temp = readl(lcm_clock + CLK_DIV_LCD);
	temp &= ~(0xf);
	writel(temp, lcm_clock + CLK_DIV_LCD);

	//Controls IP clock gating for LCD_BLK   
	//CLK_FIMD0:Gating all clocks for FIMD0 (1=Pass)
	temp = readl(lcm_clock + CLK_GATE_IP_LCD);
	temp |= 0x1;
	writel(temp, lcm_clock + CLK_GATE_IP_LCD);

	//FIMDBYPASS_LBLK0:FIMD of LBLK0 Bypass Selection (1=FIMD Bypass)
	temp = readl(lcm_ctrl + LCDBLK_CFG);
	temp |= (0x1<<1);
	writel(temp, lcm_ctrl + LCDBLK_CFG);

	//MIE0_DISPON:MIE0_DISPON: PWM output control (1=PWM outpupt enable)
	temp = readl(lcm_ctrl + LCDBLK_CFG2);
	temp |= (0x1);
	writel(temp, lcm_ctrl + LCDBLK_CFG2);

	mdelay(1000);

	//LCD时钟:  VCLK=FIMD*SCLK/(CLKVAL+1), where CLKVAL>=1
	//800/(19+1) == 40M<80M
	temp = readl(lcm_base + VIDCON0);
	temp |= (19<<6);
	writel(temp, lcm_base + VIDCON0);

	/* 3.2 配置寄存器 */
	/* VIDCON1
		FIXVCLK [10:9] : 01 = VCLK running
		IVCLK  [7] : 1 = Fetches video data at VCLK rising edge
		IHSYNC [6] : 1 = Inverted
		IVSYNC [5] : 1 = Inverted
	*/
	temp = readl(lcm_base + VIDCON1);
	temp &= ~(0x3 << 9);
	temp |= (1 << 9) | (1 << 7) | (1 << 6) | (1 << 5);
	writel(temp, lcm_base + VIDCON1);

	/* VIDTCON0
		VBPD  [[23:16] 	: Vertical back porch				tvb - tvpw + 1 = 23 - 10 ==> 12 
		VFPD  [15:8] 	: Vertical front porch				tvfp + 1 = 22 ==> 21
		VSPW  [7:0] 	: Vertical synchronization pulse	tvpw + 1 = 10 ==> 9
	*/
	temp = readl(lcm_base + VIDTCON0);
	temp |= (12 << 16) | (21 << 8) | (9);
	writel(temp, lcm_base + VIDTCON0);
	
	/* VIDTCON1
		HBPD  [[23:16] 	: Horizontal back porch				thb - thpw + 1 = 46 - 20 ==> 25
		HFPD  [15:8] 	: Horizontal front porch			thvfp + 1 = 210 ==> 209
		HSPW  [7:0] 	: Horizontal synchronization pulse	thpw + 1 = 20 ==> 19
	*/
	temp = readl(lcm_base + VIDTCON1);
	temp |= (25 << 16) | (209 << 8) | (19);
	writel(temp, lcm_base + VIDTCON1);
	
	/* VIDTCON2
		LINEVAL [21:11] : (LINEVAL + 1)				tvd + 1 = 480 ==> 479
		HOZVAL  [10:0] 	: horizontal size			thd = 800 ==> 800
	*/
	temp = readl(lcm_base + VIDTCON2);
	temp |= (479 << 11) | (800);
	writel(temp, lcm_base + VIDTCON2);
	
	/* WINCON0
		WSWP_F [15] : 1 = Enables swap
		BPPMODE_F [5:2] : 1101 = Unpacked 25 BPP (non-palletized A:1-R:8-G:8-B:8) \
							Supports unpacked 32 BPP (non-palletized A:8-R:8-G:8-B:8) for per pixel blending.
		ENWIN_F [0] : 1 = Enables the video output and video control signal
	*/
	temp = readl(lcm_base + WINCON0);
	temp &= ~(0xf << 2);
	temp |= (1 << 15) | (0xd << 2) | (1);
	writel(temp, lcm_base + WINCON0);

	/* SHADOWCON
		C0_EN_F [0] : 1 = Enables
	*/
	temp = readl(lcm_base + SHADOWCON);
	temp |= (0x1);
	writel(temp, lcm_base + SHADOWCON);

	/* WINCHMAP2
		CH0FISEL [18:16] : 001 = Window 0, Selects Channel 0's channel.
		W0FISEL [2:0] : 001 = Channel 0, Selects Window 0's channel. 
	*/
	temp = readl(lcm_base + WINCHMAP2);
	temp &= ~(7 << 16);
	temp |= (0x1 << 16);
	temp &= ~(0x7);
	temp |= (0x1);
	writel(temp, lcm_base + WINCHMAP2);

	/* OSD是on-screen display的简称，即屏幕菜单式调节方式。即在当前显示上叠加一层显示，就像显示器的调节菜单 */
	/* VIDOSD0A
		OSD_LeftTopX_F [21:11] : 0
		OSD_LeftTopY_F [10:0]  : 0

	   VIDOSD0B
	    OSD_RightBotX_F [21:11] : LCM_WIDTH
	    OSD_RightBotY_F [10:0]  : LCM_HEIGH

	   VIDOSD0C
	   	OSDSIZE [23:0] : Height * Width (number of word)
	*/
	writel(0, lcm_base + VIDOSD0A);
	writel((((LCM_WIDTH - 1) << 11) | (LCM_HEIGH - 1)), lcm_base + VIDOSD0B);
	writel(((LCM_WIDTH * LCM_HEIGH) >> 1), lcm_base + VIDOSD0C);

	/* 启动LCD显示 */
	/* VIDCON0
		ENVID [1]   : 1 = Enables the video output and display control signal
		ENVID_F [0] : 1 = Enables the video output and display control signal
		
		Display On: ENVID and ENVID_F are set to "1".
		Direct Off: ENVID and ENVID_F are set to "0" simultaneously.
		Per Frame Off: ENVID_F is set to "0" and ENVID is set to "1".
	*/
	temp = readl(lcm_base + VIDCON0);
	temp |= (0x1<<1) | (0x01);
	writel(temp, lcm_base + VIDCON0);

	/* 3.3 分配显存，并把地址告诉LCD控制器 */
	tiny4412_fb->screen_base = dma_alloc_writecombine(NULL, tiny4412_fb->fix.smem_len, (dma_addr_t *)&tiny4412_fb->fix.smem_start, GFP_KERNEL);
	if(NULL == tiny4412_fb->screen_base)
	{
		DEBUG_PRINTK("dma_alloc_writecombine error!\n");
		return -ENOMEM;
	}

	/* VIDW00ADD0B0
		VBASEU_F [31:0] : Specifies A[31:0] of the start address for video framebuffer.

	   VIDW00ADD1B0
	   	VBASEL_F [31:0] : Specifies A[31:0] of the end address for video frame buffer.
							VBASEL = VBASEU + (PAGEWIDTH + OFFSIZE) * (LINEVAL + 1)
	*/
	writel(tiny4412_fb->fix.smem_start, lcm_base + VIDW00ADD0B0);
	writel((tiny4412_fb->fix.smem_start + tiny4412_fb->fix.smem_len), lcm_base + VIDW00ADD1B0);

	/* 4.注册fb_info结构体 */
	if(0 != register_framebuffer(tiny4412_fb))
	{
		DEBUG_PRINTK("register_framebuffer error!\n");
		return -EINVAL;
	}
	
	return 0;
}


static int lcm_drv_remove(struct platform_device *pdev)
{
	DEBUG_PRINTK("------- %s -------\n",__func__);

	unregister_framebuffer(tiny4412_fb);
	dma_free_writecombine(&pdev->dev, tiny4412_fb->screen_size, tiny4412_fb->screen_base, tiny4412_fb->fix.smem_start);
	devm_iounmap(&pdev->dev, lcm_clock);
	devm_iounmap(&pdev->dev, lcm_power);
	devm_iounmap(&pdev->dev, lcm_ctrl);
	devm_iounmap(&pdev->dev, lcm_base);
	framebuffer_release(tiny4412_fb);

	return 0;
}



const struct of_device_id lcm_match_table[] = {
		{.compatible = "tiny4412,lcd_s702",},
		{ },
};


static struct platform_driver lcm_pdrv = {
	.probe	= lcm_drv_probe,
	.remove = lcm_drv_remove,
	.driver = {
		.name 			= "lcm_pdrv",
		.of_match_table = lcm_match_table,
	},
};

static int __init lcm_drv_init(void)
{
	DEBUG_PRINTK("------- %s -------\n",__func__);
	return platform_driver_register(&lcm_pdrv);
}

static void __exit lcm_drv_exit(void)
{
	DEBUG_PRINTK("------- %s -------\n",__func__);
	platform_driver_unregister(&lcm_pdrv);
}

module_init(lcm_drv_init);
module_exit(lcm_drv_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("KiFF <guibin_lin@163.com>");
MODULE_DESCRIPTION("Exynos4412 LCM driver.");
MODULE_VERSION("V1.0");






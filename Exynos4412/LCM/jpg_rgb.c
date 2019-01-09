#include <stdio.h>
#include "jpeglib.h"
#include <setjmp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <string.h>
#include <stdlib.h>

#define FB_DEVICE_NAME "/dev/fb0"

static int fd;
static struct fb_var_screeninfo fb_var;
static struct fb_fix_screeninfo fb_fix;			
static unsigned char *fb_mem;
static unsigned int screen_size;
static unsigned int line_width;
static unsigned int pixel_width;

static int fb_device_init(void)
{
	int ret;
	fd = open(FB_DEVICE_NAME, O_RDWR);
	if (fd < 0)
	{
		printf("Can't open %s\n", FB_DEVICE_NAME);
		return -1;
	}
	//��ȡ�ɱ���Ϣ
	ret = ioctl(fd, FBIOGET_VSCREENINFO, &fb_var);
	if (ret < 0)
	{
		printf("Can't get fb's var\n");
		return -1;
	}
	//��ȡ�̶���Ϣ
	ret = ioctl(fd, FBIOGET_FSCREENINFO, &fb_fix);
	if (ret < 0)
	{
		printf("Can't get fb's fix\n");
		return -1;
	}
	//ӳ��fb
	screen_size = fb_var.xres * fb_var.yres * fb_var.bits_per_pixel / 8;
	fb_mem = (unsigned char *)mmap(NULL , screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fb_mem < 0)	
	{
		printf("Can't mmap\n");
		return -1;
	}
	line_width  = fb_var.xres * fb_var.bits_per_pixel / 8;
	pixel_width = fb_var.bits_per_pixel / 8;
	return 0;
}

//color:0x00RRGGBB
static int fb_show_pixel(int x, int y, unsigned int color)
{
	unsigned char *fb_show;
	unsigned short *fb_show_16bpp;
	unsigned int *fb_show_32bpp;
	unsigned short fb_show_16bpp_new; /* 565 */
	int red;
	int green;
	int blue;
	
	if ((x >= fb_var.xres) || (y >= fb_var.yres))
	{
		printf("Out of region\n");
		return -1;
	}
	
	fb_show        = fb_mem + line_width * y + pixel_width * x;//��λ
	fb_show_16bpp  = (unsigned short *)fb_show;
	fb_show_32bpp  = (unsigned int *)fb_show;
	
	switch (fb_var.bits_per_pixel)
	{
		case 8:
			{
				*fb_show = (unsigned char)color;
				break;
			}
		case 16:
			{
				red   = (color >> (16+3)) & 0x1F;
				green = (color >> (8+2)) & 0x3F;
				blue  = (color >> 3) & 0x1F;
				fb_show_16bpp_new = (red << 11) | (green << 5) | blue;
				*fb_show_16bpp	= fb_show_16bpp_new;
				break;
			}
		case 32:
			{
				*fb_show_32bpp = color;
				break;
			}
		default :
			{
				printf("Can't support %d bpp\n", fb_var.bits_per_pixel);
				return -1;
			}
	}
	return 0;
}

static int fb_clean_screen(unsigned int back_color)
{
	unsigned char *fb_show;
	unsigned short *fb_show_16bpp;
	unsigned int *fb_show_32bpp;
	unsigned short fb_show_16bpp_new; /* 565 */
	int red;
	int green;
	int blue;
	int i = 0;
	
	fb_show       = fb_mem;
	fb_show_16bpp = (unsigned short *)fb_show;
	fb_show_32bpp = (unsigned int *)fb_show;
	
	switch (fb_var.bits_per_pixel)
	{
		case 8:
			{
				memset(fb_mem, back_color, screen_size);
				break;
			}
		case 16:
			{
				red   = (back_color >> (16+3)) & 0x1F;
				green = (back_color >> (8+2)) & 0x3F;
				blue  = (back_color >> 3) & 0x1F;
				fb_show_16bpp_new = (red << 11) | (green << 5) | blue;
				while (i < screen_size)
				{
					*fb_show_16bpp	= fb_show_16bpp_new;
					fb_show_16bpp++;
					i += 2;
				}
				break;
			}
		case 32:
			{
				while (i < screen_size)
				{
					*fb_show_32bpp	= back_color;
					fb_show_32bpp++;
					i += 4;
				}
				break;
			}
		default :
			{
				printf("Can't support %d bpp\n", fb_var.bits_per_pixel);
				return -1;
			}
	}
	return 0;
}

static int fb_show_line(int x_start, int x_end, int y, unsigned char *color_array)
{
	int i = x_start * 3;
	int x;
	unsigned int color;
	
	if (y >= fb_var.yres)
		return -1;
	if (x_start >= fb_var.xres)
		return -1;
	if (x_end >= fb_var.xres)
	{
		x_end = fb_var.xres;		
	}
	
	for (x = x_start; x < x_end; x++)
	{
		/* 0xRRGGBB */
		color = (color_array[i]<<16) + (color_array[i+1]<<8) + (color_array[i+2]<<0);
		i += 3;
		fb_show_pixel(x, y, color);
	}
	
	return 0;
}

/* 
 * Uage: jpg_rgb <jpg_file>
 */
int main(int argc, char **argv)
{
	//1������jpeg����ṹ��ռ䡢����ʼ��
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	FILE * infile;
	int row_stride;
	unsigned char *buffer;
	
	if (argc != 2)
	{
		printf("Usage: \n");
		printf("%s <jpg_file>\n", argv[0]);
		return -1;
	}
	
	if (fb_device_init())
	{
		return -1;
	}
	
	fb_clean_screen(0);
	
	//��jerr����ṹ����jpeg����ṹ��
	cinfo.err = jpeg_std_error(&jerr);
	
	//��ʼ��cinfo�ṹ��
	jpeg_create_decompress(&cinfo);
	
	//2��ָ����ѹ����Դ
	if ((infile = fopen(argv[1], "rb")) == NULL) {
		fprintf(stderr, "can't open %s\n", argv[1]);
		return -1;
	}
	
	jpeg_stdio_src(&cinfo, infile);
	
	//3����ȡ��ѹ�ļ���Ϣ
	jpeg_read_header(&cinfo, TRUE);
	
	/* Դ��Ϣ */
	printf("image_width = %d\n", cinfo.image_width);
	printf("image_height = %d\n", cinfo.image_height);
	printf("num_components = %d\n", cinfo.num_components);
	
	//4��Ϊ��ѹ�趨����������ͼ���С����ɫ�ռ�
	printf("enter scale M/N:\n");
	scanf("%d/%d", &cinfo.scale_num, &cinfo.scale_denom);
	printf("scale to : %d/%d\n", cinfo.scale_num, cinfo.scale_denom);
	
	//5����ʼ��ѹ��	
	jpeg_start_decompress(&cinfo);
	
	/* �����ͼ�����Ϣ */
	printf("output_width = %d\n", cinfo.output_width);
	printf("output_height = %d\n", cinfo.output_height);
	printf("output_components = %d\n", cinfo.output_components);
	
	//6��ȡ���ݲ���ʾ
	//һ�е����ݳ���
	row_stride = cinfo.output_width * cinfo.output_components;
	buffer = malloc(row_stride);
	
	// ѭ������jpeg_read_scanlines��һ��һ�еػ�ý�ѹ������
	while (cinfo.output_scanline < cinfo.output_height) 
	{
		(void) jpeg_read_scanlines(&cinfo, &buffer, 1);
		// д��LCDȥ
		fb_show_line(0, cinfo.output_width, cinfo.output_scanline, buffer);
	}
	
	//7����ѹ���
	jpeg_finish_decompress(&cinfo);
	
	//8���ͷ���Դ���˳�����
	free(buffer);
	jpeg_destroy_decompress(&cinfo);
	
	return 0;
}


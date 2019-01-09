#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define LED_ALL_ON	_IO('L', 0x1234)
#define LED_ALL_OFF	_IO('L', 0x1235)
#define LED_NUM_ON  _IOW('L', 0x1236, int)
#define LED_NUM_OFF _IOW('L', 0x1237, int)


/*
	usge:
		./led_app <lednum> <on/off>
*/

void usage_print(void)
{
	printf("usge:\n");
	printf("		./led_app <lednum> <on/off>\n");
	printf("lednum : 1 to 5\n");
	printf("on/off : 0 or 1\n");
}

int main(int argc, char **argv)
{
	int fd;
	int lednum = 0;
	int state = 0;

	if(argc != 3)
	{
		usage_print();
		return -1;
	}

	fd = open("/dev/LedNode", O_RDWR);
	if(fd < 0)
	{
		printf("open failed!\n");
		return -1;
	}

	lednum = strtoul(argv[1], 0, 0);
	state  = strtoul(argv[2], 0, 0);

	if((lednum == 5) && (state == 1))
	{
		ioctl(fd, LED_ALL_ON, 0);
	}
	else if((lednum == 5) && (state == 0))
	{
		ioctl(fd, LED_ALL_OFF, 0);
	}
	else if((lednum != 5) && (state == 1))
	{
		ioctl(fd, LED_NUM_ON, lednum);
	}
	else if((lednum != 5) && (state == 0))
	{
		ioctl(fd, LED_NUM_OFF, lednum);
	}
	else
	{
		usage_print();
	}
		
	if(0 != close(fd))
	{
		printf("close failed!\n");
		return -1;
	}

	return 0;
}






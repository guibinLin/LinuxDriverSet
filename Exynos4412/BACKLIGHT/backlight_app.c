#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>

int main(int argc, char **argv)
{
	int fd;
	int backlight_level = 0;

	if(argc != 2)
	{
		printf("Usage: %s <backlight_level>\n", argv[0]);
		return 0;
	}

	fd = open("/dev/backlight", O_RDWR);
	if(fd < 0)
	{
		printf("open failed!\n");
		return -1;
	}

	backlight_level = strtoul(argv[1], 0, 0);

	if(1 != write(fd, &backlight_level, 1))
	{
		printf("write backlight failed!\n");
		return -1;
	}

	if(0 != close(fd))
	{
		printf("close failed!\n");
		return -1;
	}
		

	return 0;
}




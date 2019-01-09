#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define KEY_UP			103
#define KEY_LEFT		105
#define KEY_RIGHT		106
#define KEY_DOWN		108

struct keypad_event {
	unsigned int code;
	unsigned int value;
};


int main(int argc, char **argv)
{
	int fd;
	struct keypad_event event;
	
	fd = open("/dev/keypad", O_RDWR);
	if(fd < 0)
	{
		printf("open failed!\n");
		return -1;
	}

	while(1)
	{
		if(read(fd, &event, sizeof(event)) < 0)
		{
			printf("read failed!\n");
			return -1;
		}

		if(1 == event.value)
		{
			/* press */
			switch(event.code)
			{
				case KEY_UP: 	printf("KEY_UP press!\n"); 		break;
				case KEY_DOWN: 	printf("KEY_DOWN press!\n"); 	break;
				case KEY_LEFT: 	printf("KEY_LEFT press!\n"); 	break;
				case KEY_RIGHT: printf("KEY_RIGHT press!\n"); 	break;
				default: break;
			}
			
		} else {
			/* release */
			switch(event.code)
			{
				case KEY_UP: 	printf("KEY_UP release!\n"); 	break;
				case KEY_DOWN: 	printf("KEY_DOWN release!\n"); 	break;
				case KEY_LEFT: 	printf("KEY_LEFT release!\n"); 	break;
				case KEY_RIGHT: printf("KEY_RIGHT release!\n"); break;
				default: break;
			}
		}
	}

	if(0 != close(fd))
	{
		printf("close failed!\n");
		return -1;
	}
		

	return 0;
}




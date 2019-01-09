#include <stdio.h>  
#include <sys/types.h>  
#include <sys/stat.h>  
#include <fcntl.h> 
#include <stdlib.h>
#include <unistd.h>
#include <linux/input.h>

int main(void)  
{  
	int fd;      
	char ret[2];
	struct input_event event;  
	
	
	fd = open("/dev/input/event0", O_RDONLY);
	if(fd <= 0)
	{  
		printf("open /dev/input/event0 device error!\n");  
		return 0;  
	}  
	
	while(1)  
	{     
		if(sizeof(event) == read(fd, &event, sizeof(event))) 
        {  
			if (event.type == EV_KEY)
            {  
				printf("  type: EV_KEY, event = %s, value = %d \r\n",   
						event.code == BTN_TOUCH ? "BTN_TOUCH" : "Unkown", event.value);   
			}  
			else if(event.type == EV_ABS)
            {  
				printf("  type: EV_ABS, event = %s, value = %d \r\n",   
						event.code == ABS_MT_POSITION_X ? "ABS_X" :   
						event.code == ABS_MT_POSITION_Y ? "ABS_Y" :   
						event.code == ABS_MT_TRACKING_ID ? "ABS_ID" :"Unkown", event.value); 
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



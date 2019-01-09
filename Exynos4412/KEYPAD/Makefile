CROSS_COMPILE = arm-none-linux-gnueabi-
CC = $(CROSS_COMPILE)gcc

APP = keypad_app
MODULE = keypad_drv

KERNLE_DIR = /work/android_code/linux-4.4.1
CUR_DIR = $(shell pwd)

all:
	make -C $(KERNLE_DIR) M=$(CUR_DIR) modules
	$(CC) -o $(APP) $(APP).c

install:
	cp *.ko /nfs_root/myrootfs/
	cp $(APP) /nfs_root/myrootfs/

clean:
	make -C $(KERNLE_DIR) M=$(CUR_DIR) clean
	$(RM) $(APP)

obj-m=$(MODULE).o

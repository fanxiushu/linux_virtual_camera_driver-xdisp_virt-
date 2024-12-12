### by fanxiushu 2024-11-16
#####
obj-m += avstrm_virt.o
avstrm_virt-objs := init.o main.o ioctl.o video_ioctl.o video_buffer.o

KERNEL_DIR=/lib/modules/$(shell uname -r)/build
PWD=$(shell pwd)

#### 这是针对有浮点运行的init.c文件添加变量,不过后来转成整数运算，不再需要
### CFLAGS_init.o += -mhard-float -msse  -msse2

all:
	make -C $(KERNEL_DIR) M=$(PWD) modules
clean:
	-rm -fr *.mod.c *.ko *.o *.cmd .* *.order *.symvers


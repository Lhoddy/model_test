obj-m := lhd_kernel.o             #产生模块的目标文件
lhd_kernel-objs := dhmp_server.o  dhmp_dev.o dhmp_log.o dhmp_transport.o
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules    #编译模块
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean      #清理
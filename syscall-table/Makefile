obj-m += the_usctm.o
the_usctm-objs += usctm.o lib/vtpmo.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

insmod:
	insmod the_usctm.ko 

rmmod:
	rmmod the_usctm

# USAGE:
# make
# sudo make insmod
#
# make clean
# sudo make rmmod

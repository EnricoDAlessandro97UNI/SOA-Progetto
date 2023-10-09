obj-m += blocklevel_module.o
blocklevel_module-objs += blocklevel.o lib/scth.o singlefilefs/file.o singlefilefs/dir.o utils.o

A = $(shell cat /sys/module/the_usctm/parameters/sys_call_table_address)

NBLOCKS := 6	# NBLOCKS includes also the superblock and the inode

KVERSION = $(shell uname -r)

all:
	gcc singlefilefs/singlefilemakefs.c -o singlefilefs/singlefilemakefs
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) modules
	gcc user/user.c -o user/user
	gcc user/test.c -o user/test -lpthread

clean:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) clean
	rm ./singlefilefs/singlefilemakefs
	rm ./user/user
	rm ./user/test
	rmdir ./mount

insmod:
	insmod blocklevel_module.ko the_syscall_table=$(A)

rmmod:
	rmmod blocklevel_module

create-fs:
	dd bs=4096 count=$(NBLOCKS) if=/dev/zero of=image
	./singlefilefs/singlefilemakefs image $(NBLOCKS)
	mkdir mount

mount-fs:
	mount -o loop -t singlefilefs image ./mount/

umount-fs:
	umount ./mount/

obj-m := ghost.o
ghost-objs := ghost_main.o
MY_CFLAGS += -g -DDEBUG
ccflags-y += ${MY_CFLAGS}
CC += ${MY_CFLAGS}
EXTRA_CFLAGS := -I.

all:
		make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules $(EXTRA_CFLAGS)

clean:
		make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

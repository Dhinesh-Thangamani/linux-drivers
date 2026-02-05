obj-m += cpu_stress.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
    $(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
    $(MAKE) -C $(KDIR) M=$(PWD) clean

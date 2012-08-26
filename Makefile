obj-m += drddr.o
obj-m += caferace.o
obj-m += test.o
obj-m += timeout.o

drddr-objs := drddr-main.o drddr-utils.o drddr-wp.o drddr-bp.o drddr-mon.o disassem.o

.PHONY: all, test, clean

all: drddr-shell
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
test: disassem-test
	/bin/true
disassem.o: all
	/bin/true

disassem-test: disassem.h disassem.o disassem-test.o
	gcc disassem.o disassem-test.o -o disassem-test

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
	-rm disassem-test
	-rm drddr-shell

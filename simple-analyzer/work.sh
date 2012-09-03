#!/bin/bash
objdump -d /lib/modules/$(uname -r)/build/vmlinux > vmlinux.objd
objdump -d /lib/modules/$(uname -r)/build/fs/built-in.o > fs.objd
./simple -i fs.objd -O -o fs.regex
./simple -f fs.regex -o fs.addr

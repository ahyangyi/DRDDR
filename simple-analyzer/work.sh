#!/bin/bash

SUBPROJECT=fs

make
objdump -d /lib/modules/$(uname -r)/build/vmlinux > vmlinux.objd
objdump -d /lib/modules/$(uname -r)/build/$SUBPROJECT/built-in.o > $SUBPROJECT.objd
./simple -i $SUBPROJECT.objd -O -o $SUBPROJECT.regex
./simple -f $SUBPROJECT.regex -o $SUBPROJECT.addr

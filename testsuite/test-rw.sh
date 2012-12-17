#!/bin/bash

echo "Compiling kernel modules..."
make
(cd ..; cd drddr; make)

echo "Inserting the micro benchmark into the kernel..."
insmod rw.ko
sleep 1
dmesg | sed -n "s/.*[WRITER|READER] ADDRESS..\(.*\)$/\1/gp" | tail -n 2 > address.txt
echo "The address list to monitor is:"
cat address.txt

echo "Inserting drddr and starting debugging..."
insmod ../drddr/drddr.ko
echo "load address.txt" | ../drddr/drddr-shell
echo "start" | ../drddr/drddr-shell

sleep 5

echo "Removing modules..."
rmmod ../drddr/drddr.ko
rmmod rw.ko

echo "Result:"
dmesg | grep 'Debug interruption!!!!! (OUCH!)' -B1 -A27 | tail -n 29

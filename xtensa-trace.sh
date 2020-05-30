#!/bin/bash

# Simple script to trace execution. Can be vastly improved upon.
#
# Usage.
#
# 1) First run qemu with trace option -t and output to file "dump.txt"
# ./xtensa-host.sh apl -r ../../sof/sof/build_apl_gcc/src/arch/xtensa/rom-apl.bin \
#                      -k ../../zephyrproject/zephyr/build/zephyr/zephyr.ri \
#                      -t > dump.txt 2>&1
#
# 2) Now run this script.
#

# Find all xtensa function "entry" instructions.
grep entry dump.txt > entry.txt

# Now find the function name for each "entry" instruction address.
while read input
do
	grep `echo $input | cut -f1 -d: | cut -c 3-10` /home/lrg/work/zephyrproject/zephyr/build/zephyr/zephyr.lst | grep -v call | grep -v entry -m 1
done < entry.txt

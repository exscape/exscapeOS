#!/bin/bash

echo
echo Removing old sources and build files...
echo
rm -rf binutils-2.*.* gcc-4.*.* gdb-7.*.* newlib-*.*.* build-{binutils,gcc,newlib,gdb}

#!/bin/sh

if [[ `basename $PWD` != "toolchain" ]]; then
	echo "This script (build.sh) must be run from the toolchain directory."
	exit 1
fi

# Set these up!
DL=1          # Download distfiles automatically?
FORCE_CLEAN=1 # Remove all unpacked sources/build stuff and re-unpack and patch
BUILD_GDB=1   # Build a GDB with exscapeos target? (Set to 0 if you're not going to debug, to save time/disk space.)
export PREFIX=/usr/local/cross # Where to install everything

sed -e "s#PREFIX = .*#PREFIX = $PREFIX#g" -i ../Makefile

MAC=0 # checked below automatically
if gcc --version | grep -iq llvm; then
	MAC=1
fi

# Should not be changed
export TARGET=i586-pc-exscapeos

err() {
	echo
	echo "Error: last command failed with exit status $?"
	exit 1
}

cleanupandexit() {
unset CFLAGS_FOR_TARGET
unset CFLAGS
unset CFLAGS_FOR_BUILD

if [[ $MAC -eq 1 ]]; then
	unset CC
	unset CXX
	unset CPP
	unset LD
	unset MAC
fi
}

# These MUST point to files from automake 1.12.x OR OLDER!
# Newlib unfortunately requires features that were removed in 1.13.
#
export AUTOMAKE=$PREFIX/bin/automake
export ACLOCAL=$PREFIX/bin/aclocal

if [[ $MAC -eq 1 ]]; then
	if [[ ! -f "/usr/bin/gcc-4.2" || ! -f "/usr/bin/g++-4.2" || ! -f "/usr/bin/cpp-4.2" ]]; then
		echo "/usr/bin{gcc,g++,cpp}-4.2 not found! These are required for Mac OS X builds."
		exit 1
	fi
	export CC=/usr/bin/gcc-4.2
	export CXX=/usr/bin/g++-4.2
	export CPP=/usr/bin/cpp-4.2
	export LD=/usr/bin/gcc-4.2
fi

if [[ $DL -eq 1 ]]; then
	echo
	echo 'Downloading distfiles (if necessary)...'
	echo
	mkdir -p distfiles
	cd distfiles

	if [[ $NEWLIB_ONLY -ne 1 ]]; then

		if [[ ! -f "binutils-2.23.1.tar.bz2" ]]; then
			wget 'http://ftp.gnu.org/gnu/binutils/binutils-2.23.1.tar.bz2' || err
		fi

		if [[ ! -f "gcc-4.8.2.tar.bz2" ]]; then
			wget 'ftp://ftp.gwdg.de/pub/misc/gcc/releases/gcc-4.8.2/gcc-4.8.2.tar.bz2' || err
		fi

		if [[ $BUILD_GDB -ne 0 && ! -f "gdb-7.6.2.tar.bz2" ]]; then
			wget 'http://ftp.gnu.org/gnu/gdb/gdb-7.6.2.tar.bz2' || err
		fi
	fi

		if [[ ! -f "newlib-1.20.0.tar.gz" ]]; then 
			wget 'ftp://sources.redhat.com/pub/newlib/newlib-1.20.0.tar.gz' || err
		fi

	cd ..
fi

if [[ $FORCE_CLEAN -eq 1 ]]; then
	bash clean.sh || err

	echo
	echo Unpacking sources...
	if [[ $NEWLIB_ONLY -ne 1 ]]; then
		for FILE in distfiles/{binutils-2.23.1.tar.bz2,gcc-4.8.2.tar.bz2,newlib-1.20.0.tar.gz}; do echo "$FILE ..."; tar xf $FILE || err; done
		if [[ $BUILD_GDB -ne 0 ]]; then
			tar xf distfiles/gdb-7.6.2.tar.bz2 || er
		fi
	else
		tar xf distfiles/newlib-1.20.0.tar.gz || err
	fi
	echo
fi

mkdir -p build-newlib

if [[ $NEWLIB_ONLY -ne 1 ]]; then

mkdir -p build-{binutils,gcc}

echo
echo Patching binutils...
echo
cd binutils-2.23.1
patch -p1 < ../patches/binutils-2.23.1-exscapeos.patch || err
cd ..
cd build-binutils

echo
echo Configuring binutils...
echo
CFLAGS="-Wno-error" ../binutils-2.23.1/configure --target=$TARGET --prefix=$PREFIX --disable-nls || err

echo
echo Building binutils...
echo
make -j8 all || err

echo 
echo Installing binutils...
echo 
make install || err

cd ..

echo
echo Patching GCC...
echo
cd gcc-4.8.2
patch -p1 < ../patches/gcc-4.8.2-exscapeos.patch || err
cd ..

echo 
echo Configuring GCC...
echo
cd build-gcc
../gcc-4.8.2/configure --target=$TARGET --prefix=$PREFIX --disable-nls --enable-languages=c --with-gmp=/opt/local --with-mpfr=/opt/local --with-mpc=/opt/local || err

echo
echo Building GCC and libgcc...
echo
make -j8 all-gcc || err
make -j8 all-target-libgcc || err

echo 
echo Installing GCC and libgcc...
echo
make install-gcc || err
make install-target-libgcc || err
cd ..


if [[ $BUILD_GDB -ne 0 ]]; then
mkdir build-gdb
echo
echo Patching gdb...
echo
cd gdb-7.6.2
patch -p1 < ../patches/gdb-7.6.2-exscapeos.patch || err
cd ..

echo
echo Configuring gdb...
echo
cd build-gdb
../gdb-7.6.2/configure --target=$TARGET --prefix=$PREFIX --disable-werror || err

echo
echo Building gdb...
echo
make -j8 || err

echo
echo Installing gdb...
echo
make install || err
cd ..
fi # BUILD_GDB != 0

fi # end $NEWLIB_ONLY != 1

# Only necessary for Newlib, from the looks of it
# That would make sense, since Newlib is the only part of this
# that builds code *for* exscapeOS, not just code that *targets* it
export PATH=$PATH:$PREFIX/bin
export CFLAGS_FOR_TARGET="-O0 -gstabs+"
export CFLAGS="-O0 -gstabs+"
export CFLAGS_FOR_BUILD="-O0 -gstabs+"

# These MUST point to files from automake 1.12.x OR OLDER!
# 
export AUTOMAKE=$PREFIX/bin/automake
export ACLOCAL=$PREFIX/bin/aclocal

echo
echo Patching Newlib...
echo
cd newlib-1.20.0
patch -p1 < ../patches/newlib-1.20.0-exscapeos.patch || err
mkdir -p newlib/libc/sys/exscapeos
cp -Rf ../patches/exscapeos/* newlib/libc/sys/exscapeos/ || err
#cp ../patches/exscapeos/getreent.c newlib/libc/reent/getreent.c || err # I'm not a fan of doing this!

cd newlib/libc/sys
autoconf || err
cd exscapeos
autoreconf || err
cd ../../../..
cd ..

echo
echo Configuring Newlib...
echo
cd build-newlib
../newlib-1.20.0/configure --target=$TARGET --prefix=$PREFIX || err

echo
echo Building Newlib...
echo
make -j8 || err

echo
echo Installing Newlib...
echo
make install || err

cd ..

echo
echo Successfully build and installed exscapeOS toolchain to $PREFIX'!'
echo

cleanupandexit

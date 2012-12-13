#!/bin/sh

###
### TODO: error checking!
###

# Set these up!
DL=0
FORCE_CLEAN=1
MAC=1

err() {
	echo
	echo "Error: last command failed with exit status $?"
	exit 1
}

if [[ $MAC -eq 1 ]]; then
	export CC=/usr/bin/gcc-4.2
	export CXX=/usr/bin/g++-4.2
	export CPP=/usr/bin/cpp-4.2
	export LD=/usr/bin/gcc-4.2
fi

if [[ $DL -eq 1 ]]; then
	echo
	echo Downloading distfiles...
	echo
	mkdir -p distfiles
	cd distfiles
	wget 'http://ftp.gnu.org/gnu/binutils/binutils-2.23.1.tar.bz2' || err
	wget 'ftp://ftp.gwdg.de/pub/misc/gcc/releases/gcc-4.7.2/gcc-4.7.2.tar.bz2' || err
	wget 'ftp://sources.redhat.com/pub/newlib/newlib-1.20.0.tar.gz' || err
	cd ..
fi

if [[ $FORCE_CLEAN -eq 1 ]]; then
	bash clean.sh || err

	echo
	echo Unpacking sources... 
	for FILE in distfiles/{binutils-2.23.1.tar.bz2,gcc-4.7.2.tar.bz2,newlib-1.20.0.tar.gz}; do echo "$FILE ..."; tar xf $FILE || err; done
	echo
fi

export TARGET=i586-pc-exscapeos
export PREFIX=/usr/local/cross

mkdir -p build-{binutils,gcc,newlib}

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
../binutils-2.23.1/configure --target=$TARGET --prefix=$PREFIX --disable-nls || err

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
cd gcc-4.7.2
patch -p1 < ../patches/gcc-4.7.2-exscapeos.patch || err
cd ..

echo 
echo Configuring GCC...
echo
cd build-gcc
../gcc-4.7.2/configure --target=$TARGET --prefix=$PREFIX --disable-nls --enable-languages=c --with-gmp=/opt/local --with-mpfr=/opt/local --with-mpc=/opt/local || err

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

if [[ $MAC -eq 1 ]]; then
	unset CC
	unset CXX
	unset CPP
	unset LD
fi

#!/bin/bash

if [[ $(basename $PWD) != "contrib" ]]; then
	echo 'This script must be run from the contrib directory, for now. (Ugh!)'
	exit 1
fi

if [[ -f "../initrd/bin/lua" ]]; then
	if [[ $1 != "-f" ]]; then
		echo 'Lua already installed; skipping (use -f to force reinstall)'
		exit 0
	fi
fi

W=$PWD
pushd /tmp &&
if [[ ! -f "lua-5.2.3.tar.gz" ]]; then
wget http://www.lua.org/ftp/lua-5.2.3.tar.gz; fi &&
tar xf lua-5.2.3.tar.gz &&
cd lua-5.2.3 &&
patch -p1 < $W/lua-5.2.3-exscapeos.patch &&
make -j8 &&
cp -f src/lua $W/../initrd/bin/lua &&
echo 'Successfully built and installed Lua!'

popd

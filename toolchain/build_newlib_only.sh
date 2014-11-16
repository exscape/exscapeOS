#!/bin/bash

pushd $(dirname $0) > /dev/null
P=$(pwd)
popd >/dev/null

export NEWLIB_ONLY=1
bash -x $P/build.sh
unset NEWLIB_ONLY

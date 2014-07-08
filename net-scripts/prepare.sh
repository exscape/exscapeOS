#!/bin/bash
if [[ ! -e "/dev/tap" ]]; then 
	sudo kextload /Library/Extensions/tap.kext
	sudo ln -s /dev/tap2 /dev/tap
fi

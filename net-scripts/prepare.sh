#!/bin/bash
if [[ ! -e "/dev/tap" ]]; then 
	sudo ln -s /dev/tap2 /dev/tap
fi

#!/bin/bash

sudo ifconfig tap2 192.168.10.1 &&
sudo sysctl -w net.inet.ip.forwarding=1

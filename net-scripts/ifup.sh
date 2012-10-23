#!/bin/bash

sudo ifconfig tap2 192.168.10.1 &&
sudo sysctl -w net.inet.ip.forwarding=1 &&
sudo natd -interface en0 2>/dev/null
sudo ipfw -f flush &&
sudo ipfw add divert natd all from any to any via en0 &&
sudo ipfw add divert natd all from any to any via tap2

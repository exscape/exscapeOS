#!/bin/bash

make && ~/simics/simics -e '$create_network=no' -e '$memory_megs=64' -e '$num_cpus=1' -e '$text_console=no' ~/simics/exscapeOS.simics

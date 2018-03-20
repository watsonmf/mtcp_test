#!/bin/bash

DPDK_VERSION=dpdk-18.02

ln -s "$PWD/$DPDK_VERSION/x86_64-native-linuxapp-gcc/include" "$PWD/dpdk/include"
ln -s "$PWD/$DPDK_VERSION/x86_64-native-linuxapp-gcc/lib" "$PWD/dpdk/lib"


#./configure --with-dpdk-lib="$PWD/dpdk"

#make

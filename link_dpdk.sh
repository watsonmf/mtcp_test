#!/bin/bash

DPDK_VERSION=dpdk-17.11

ln -s "$PWD/$DPDK_VERSION/x86_64-native-linuxapp-gc/include" "$PWD/dpdk/include"
ln -s "$PWD/$DPDK_VERSION/x86_64-native-linuxapp-gc/lib" "$PWD/dpdk/lib"


#./configure --with-dpdk-lib="$PWD/dpdk"

#make
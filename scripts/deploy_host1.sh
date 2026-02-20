#!/bin/bash

WORK_DIR=~/nfs/original_module
IFACE=enp137s0f0np0

if [ ! -d "$WORK_DIR" ]; then
    echo "Error: Directory '$WORK_DIR' does not exist."
    exit 1
fi

if ! ip link show "$IFACE" > /dev/null 2>&1; then
    echo "Error: Network interface '$IFACE' does not exist."
    exit 1
fi

pushd "$WORK_DIR" > /dev/null

echo "Entering $WORK_DIR and loading modules..."

sudo insmod ./ib_core.ko
sudo insmod ./ib_uverbs.ko
sudo insmod ./mlxfw.ko
sudo modprobe tls
sudo modprobe pci-hyperv-intf
sudo modprobe psample
sudo insmod ./mlx5_core.ko
sudo insmod ./mlx5_ib.ko

echo "Configuring interface $IFACE..."
sudo ifconfig "$IFACE" mtu 9000 up

popd > /dev/null

echo "Done."
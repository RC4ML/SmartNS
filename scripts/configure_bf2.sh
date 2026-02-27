#!/bin/bash

if ! ip link show bond0 > /dev/null 2>&1; then
    echo "bond0 does not exist. Creating and configuring bond0..."
    
    sudo ip link add bond0 type bond
    sudo ip link set bond0 down
    sudo ip link set bond0 type bond miimon 100 mode 4 xmit_hash_policy layer3+4
    sudo ip link set p0 down
    sudo ip link set p1 down
    sudo ip link set p0 master bond0
    sudo ip link set p1 master bond0
    sudo ip link set p0 up
    sudo ip link set p1 up
    sudo ip link set bond0 up
else
    echo "bond0 already exists. Skipping creation."
fi

sudo ifconfig bond0 mtu 9000 up
sudo ifconfig p0 mtu 9000 up
sudo ifconfig p1 mtu 9000 up
sudo ifconfig enp3s0f0s0 mtu 9000 up
sudo ifconfig pf0hpf mtu 9000 up
sudo ifconfig en3f0pf0sf0 mtu 9000 up
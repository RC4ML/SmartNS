#!/bin/bash

THIS_DIR=$(dirname $0)

# remove driver
grep smartns /proc/devices >/dev/null && sudo /sbin/rmmod smartns

# insert driver
sudo /sbin/insmod ./smartns.ko

# create device inodes
major=`fgrep smartns /proc/devices | cut -b 1-4`
echo "INFO: driver major is $major"

# remove old inodes just in case
if [ -e /dev/smartns ]; then
    sudo rm /dev/smartns
fi

echo "INFO: creating /dev/smartns inode"
sudo mknod /dev/smartns c $major 0
sudo chmod a+w+r /dev/smartns
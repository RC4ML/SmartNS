# Installation

This document shows all of the essential software installation process on test machine. 

BE ATTENTION that **We already install all dependencies on our artifact machines.**

## 1. Initial BF3:

Please refer DOCA document https://docs.nvidia.com/doca/archive/3-1-0/bf-bundle+installation+and+upgrade/index.html to initial BF3 SmartNIC.

We already initialize on AE machine.

## 2. Install NFS/NTP:

NFS is very useful for running experiment and scripts, here is the samples.

~~~bash
# NFS server's /etc/exports
sudo vim /etc/exports
/home/cxz/study    10.130.142.0/24(rw,sync,no_subtree_check,all_squash,anonuid=1001,anongid=1001)
sudo service nfs-server restart

# NFS client's /etc/fstab
sudo vim /etc/fstab
10.130.142.26:/home/cxz/study /home/cxz/study nfs defaults 0 0
sudo mount -a
~~~

NTP is used to avoid clock skew on ninja build
~~~bash
sudo apt install ntp
sudo vim /etc/ntp.conf
comment all line start with pool
add following line
server 10.130.142.26 minpoll 3 maxpoll 3
~~~

We already deploy NFS and NTP both on host and BF3 Arm sides.


## 3. Install DOCA/MLNX_OFED on host

SmartNS doesn't rely on DOCA API for using BF3, instead of standard IB verbs API and invoke BF3 function by construct special opcode.

However, DOCA-all package include MLNX_OFED package, so we can just install DOCA-all package on host simply.

Please refer this [link](https://developer.nvidia.com/doca-downloads?deployment_platform=Host-Server&deployment_package=DOCA-Host&target_os=Linux&Architecture=x86_64&Profile=doca-all) to install, we already install host DOCA-all on AE machines.

## 4. Install customized libmlx5.so and libibverbs.so

We use customized rdma-core for high performance DMA and cache invalid operations, please build and install libmlx5.so and mlx5dv.h like following:

Be attention, we need build customized libmlx5.so for both Host and Arm side! Although we only modify a little in libmlx5, however some ABI is not compatible, so we maybe also need to install libibverbs

~~~bash
git clone https://github.com/cxz66666/rdma-core
cd rdma-core
git checkout bf3_specifical

# on host side
mkdir build_host
cd build_host && cmake .. && make -j
sudo mv /lib/x86_64-linux-gnu/libmlx5.so.1.25.58.0 /lib/x86_64-linux-gnu/libmlx5.so.bak
sudo mv /lib/x86_64-linux-gnu/libibverbs.so.1.14.58.0 /lib/x86_64-linux-gnu/libibverbs.so.bak

sudo cp ./lib/libmlx5.so.1.25.55.0 /lib/x86_64-linux-gnu/
sudo cp ./lib/libibverbs.so.1.14.55.0 /lib/x86_64-linux-gnu/

sudo rm /lib/x86_64-linux-gnu/libmlx5.so.1 /lib/x86_64-linux-gnu/libibverbs.so.1 

sudo ln -sf /lib/x86_64-linux-gnu/libmlx5.so.1.25.55.0 /lib/x86_64-linux-gnu/libmlx5.so.1
sudo ln -sf /lib/x86_64-linux-gnu/libibverbs.so.1.14.58.0 /lib/x86_64-linux-gnu/libibverbs.so.1

sudo ldconfig
sudo cp ./include/infiniband/mlx5dv.h  /usr/include/infiniband/


#on arm side
mkdir build_dpu
cd build_dpu && cmake .. && make -j
sudo mv /lib/aarch64-linux-gnu/libmlx5.so.1.25.58.0 /lib/aarch64-linux-gnu/libmlx5.so.bak
sudo mv /lib/aarch64-linux-gnu/libibverbs.so.1.14.58.0 /lib/aarch64-linux-gnu/libibverbs.so.bak

sudo cp ./lib/libmlx5.so.1.25.55.0 /lib/aarch64-linux-gnu/
sudo cp ./lib/libibverbs.so.1.14.55.0 /lib/aarch64-linux-gnu/


sudo rm /lib/aarch64-linux-gnu/libmlx5.so.1 /lib/aarch64-linux-gnu/libibverbs.so.1

sudo ln -sf /lib/aarch64-linux-gnu/libmlx5.so.1.25.55.0 /lib/aarch64-linux-gnu/libmlx5.so.1
sudo ln -sf /lib/aarch64-linux-gnu/libibverbs.so.1.14.58.0 /lib/aarch64-linux-gnu/libibverbs.so.1

sudo ldconfig
sudo cp ./include/infiniband/mlx5dv.h  /usr/include/infiniband/
~~~


## 5. Modify boot grub file

On host side, we need update GRUB_CMDLINE_LINUX in /etc/default/grub like following
~~~bash
sudo vim /etc/default/grub
GRUB_CMDLINE_LINUX="intel_iommu=off iommu=pt pci=realloc=off"
sudo update-grub
~~~

On BF3 Arm side, we need isolate some Arm processes for test application
~~~bash
sudo vim /etc/default/grub
GRUB_CMDLINE_LINUX=".... keep unmodify... isolcpus=0-11 nohz_full=0-11"
sudo update-grub
~~~


## 6. Bond two BF3 port into one port
We only have 2x200Gbps BF3, so we bond two port and provided as a 400Gbps interface for upper appliaction, if you euqip with 1x400Gbps BF3, this step can skip.

Please run following commands on Arm side!!!
~~~bash
sudo mst start
sudo mlxconfig -d /dev/mst/mt41692_pciconf0  s HIDE_PORT2_PF=True NUM_OF_PF=1
sudo mlxconfig -d /dev/mst/mt41692_pciconf0.1  s HIDE_PORT2_PF=True NUM_OF_PF=1
sudo mlxconfig -d /dev/mst/mt41692_pciconf0 s LAG_RESOURCE_ALLOCATION=1
sudo mlxconfig -d /dev/mst/mt41692_pciconf0.1 s LAG_RESOURCE_ALLOCATION=1

sudo vim /etc/mellanox/mlnx-bf.conf
# add following line in this file
LAG_HASH_MODE="yes"

sudo ovs-vsctl del-port ovsbr1 p0
sudo ovs-vsctl del-port ovsbr2 p1
~~~

Then, do POWER CYCLE (cold reboot) to let the config take effect.

After reboot, exec following commands(These commands need to exec every boot time):
~~~bash
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
# only need add-port one time
sudo ovs-vsctl add-port ovsbr1 bond0 
~~~


## 7. Setup tmfifo_net0 IP
Host side have tmfifo_net0 interface， we need set this interface IP to 192.168.100.1/30
~~~bash
sudo vim /etc/netplan/50-cloud-init.yaml
# add following lines
    tmfifo_net0:
      addresses:
        - 192.168.100.1/30
      optional: true
~~~


## 8. Install required libs:

~~~bash
sudo apt install cmake libgflags-dev libnuma-dev libpci-dev
~~~

## 9. Allocate enough 2M hugepage

We use systemd service to enable allocate memory after bootup.

~~~bash
# /etc/systemd/system/hugetlb-reserve-pages.sh

#!/bin/sh
nodes_path=/sys/devices/system/node/
if [ ! -d $nodes_path ]; then
        echo "ERROR: $nodes_path does not exist"
        exit 1
fi

reserve_pages()
{
        echo $1 > $nodes_path/$2/hugepages/hugepages-2048kB/nr_hugepages
}

reserve_pages 2048 node0
reserve_pages 2048 node1
~~~

~~~bash
#/etc/systemd/system/hugetlb-pages.service

[Unit]
Description=HugeTLB Gigantic Pages Reservation
DefaultDependencies=no
Before=dev-hugepages.mount
ConditionPathExists=/sys/devices/system/node

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/etc/systemd/system/hugetlb-reserve-pages.sh

[Install]
WantedBy=sysinit.target
~~~


### 10. Check Building all SmartNS software

Now we create `build_host` and `build_dpu` directory, and build all the software.
~~~bash
git clone --recursive https://github.com/RC4ML/SmartNS
cd SmartNS

# on host
cd build_host
cmake ..
make -j

# on arm
cd build_dpu
cmake ..
make -j
~~~

It should report no error. And we will get the output binary in the build directory.
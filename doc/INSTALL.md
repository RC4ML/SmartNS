# Installation

This document describes the essential software installation and system configuration steps on the test machines.

**Note:** We have already installed all dependencies on the artifact machines.

## 1. Initialize BF3

Please refer to the DOCA document below to initialize the BF3 SmartNIC:

https://docs.nvidia.com/doca/archive/3-1-0/bf-bundle+installation+and+upgrade/index.html

Initialization has already been completed on the AE machines.

## 2. Install NFS/NTP

NFS is useful for running experiments and scripts. Example configuration:

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

NTP is used to avoid clock skew during ninja builds:

~~~bash
sudo apt install ntp
sudo vim /etc/ntp.conf
comment all line start with pool
add following line
server 10.130.142.26 minpoll 3 maxpoll 3
~~~

NFS and NTP are already deployed on both host and BF3 Arm sides.

## 3. Install DOCA/MLNX_OFED on host

SmartNS does not rely on the DOCA API. It uses the standard IB verbs API and invokes BF3 functions by constructing special opcodes.

The `doca-all` package includes MLNX_OFED, so you can install `doca-all` directly on host machines, or you can install `MLNX_OFED` instead.

Please refer to this [link](https://developer.nvidia.com/doca-downloads?deployment_platform=Host-Server&deployment_package=DOCA-Host&target_os=Linux&Architecture=x86_64&Profile=doca-all). We have already installed host `doca-all` on the AE machines.

## 4. Install customized `libmlx5.so` and `libibverbs.so`

We use a customized `rdma-core` for high-performance DMA and cache invalidation operations. Please build and install `libmlx5.so`, `libibverbs.so`, and `mlx5dv.h` as follows.

Please note that the customized `libmlx5.so` is required on both host and Arm sides. Although the modification in `libmlx5` is small, some ABI details are not fully compatible, so `libibverbs` may also need to be replaced.

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

## 5. Modify boot GRUB file

On the host side, update `GRUB_CMDLINE_LINUX` in `/etc/default/grub` as follows:

~~~bash
sudo vim /etc/default/grub
GRUB_CMDLINE_LINUX="intel_iommu=off iommu=pt pci=realloc=off"
sudo update-grub
~~~

On the BF3 Arm side, isolate some Arm cores for test applications:

~~~bash
sudo vim /etc/default/grub
GRUB_CMDLINE_LINUX=".... keep unmodify... isolcpus=0-11 nohz_full=0-11"
sudo update-grub
~~~

## 6. Bond two BF3 ports into one port

We use 2x200Gbps BF3 and bond the two ports into one 400Gbps interface for upper applications. If your platform is equipped with 1x400Gbps BF3, you can skip this step.

Please run the following commands on the Arm side:

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

Then perform a power cycle (cold reboot) to make the configuration take effect.

After reboot, execute the following commands (these commands must be executed after every boot):

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

## 7. Set up `tmfifo_net0` IP

The host side has a `tmfifo_net0` interface. Set its IP to `192.168.100.1/30`.

~~~bash
sudo vim /etc/netplan/50-cloud-init.yaml
# add following lines
    tmfifo_net0:
      addresses:
        - 192.168.100.1/30
      optional: true
~~~

## 8. Install required libraries

~~~bash
sudo apt install cmake libgflags-dev libnuma-dev libpci-dev
~~~

## 9. Allocate enough 2M huge pages

We use a systemd service to allocate memory after boot.

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

## 10. Build all SmartNS software

Now create `build_host` and `build_dpu`, then build all software:

~~~bash
git clone --recursive https://github.com/RC4ML/SmartNS
cd SmartNS
mkdir -p build_host build_dpu

# on host
cd build_host
cmake ..
make -j

# on arm
cd ../build_dpu
cmake ..
make -j
~~~

The build should finish without errors, and binaries will be generated in the corresponding build directories.

# Connect, Configure, and Deploy SmartNS

We provide two machines for AE reviewers. Each machine is equipped with an NVIDIA BlueField-3 B3220 400GbE NIC and connected back-to-back using two 200GbE QSFP56 cables. The topology is shown in the figure below. We use **`Host1` `BF1` `Host2` `BF2`** as labels in this document.

![machine status](./image/machine.png)

Applications and the kernel module run on hosts, while `smartns_dpu` (the core program) runs on BlueField-3 Arm processors.

**All prerequisites in `INSTALL.md` have already been completed** on our artifact machines. Reviewers can start from this document directly.

**AE machine access details** (jump host / key login) are moved to [APPENDIX.md](./APPENDIX.md) so the main workflow stays concise.

## 1. Hosts configuration

Use this section after each reboot.

### 1.1 Recommended: all-in-one scripts

Run the following on each machine:

| Machine | Command |
| ------- | ------- |
| `Host1` | `bash ./scripts/configure_host1.sh` |
| `Host2` | `bash ./scripts/configure_host2.sh` |
| `BF1` | `bash ./scripts/configure_bf1.sh` |
| `BF2` | `bash ./scripts/configure_bf2.sh` |

These scripts configure host RDMA kernel modules, BF bond interfaces, and MTU settings.

### 1.2 Alternative: manual steps (equivalent to scripts)

#### 1.2.1 Load host RDMA kernel modules

Execute on `Host1` and `Host2`:

~~~bash
# we already prepare the necessary original kernel module on ~/nfs/original_module
cd ~/nfs/original_module
sudo insmod ./ib_core.ko
sudo insmod ./ib_uverbs.ko
sudo insmod ./mlxfw.ko
sudo modprobe tls
sudo modprobe pci-hyperv-intf
sudo modprobe psample
sudo insmod ./mlx5_core.ko
sudo insmod ./mlx5_ib.ko
~~~

#### 1.2.2 Verify and configure BF bond interfaces

On `BF1` and `BF2`, verify:

~~~bash
ifconfig | grep bond
~~~

If you do not see output similar to `bond0: flags=5187<UP,BROADCAST,RUNNING,MASTER,MULTICAST>`, execute:

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
~~~

#### 1.2.3 Set MTU to 9000

On `Host1`:

~~~bash
sudo ifconfig enp137s0f0np0 mtu 9000 up
~~~

On `Host2`:

~~~bash
sudo ifconfig enp7s0f0np0 mtu 9000 up
~~~

On `BF1` and `BF2`:

~~~bash
sudo ifconfig bond0 mtu 9000 up
sudo ifconfig p0 mtu 9000 up
sudo ifconfig p1 mtu 9000 up
sudo ifconfig enp3s0f0s0 mtu 9000 up
sudo ifconfig pf0hpf mtu 9000 up
sudo ifconfig en3f0pf0sf0 mtu 9000 up
~~~

#### 1.3 Connectivity sanity check

On `Host1`:

~~~bash
ping -s 8192 10.0.0.200
~~~

## 2. Build SmartNS

SmartNS includes three components:

- `smartns_dpu` (runs on Arm processors)
- Linux kernel module (runs on hosts)
- User-layer applications (run on both hosts and Arm)

~~~bash
cd ~/nfs
git clone --recursive https://github.com/RC4ML/SmartNS
cd SmartNS

# execute on host
mkdir -p build_host
cd build_host
cmake ..
make -j # build test code and lib

cd ../kernel
make -j # build kernel module

# execute on ARM! (BF1 and BF2)
cd ~/nfs
mkdir -p build_dpu
cd build_dpu
cmake ..
make -j # build smartns_dpu and test code
~~~

## 3. Deploy SmartNS

> [!WARNING]
> Not all experiments require deploying SmartNS. Check [EXP.md](./EXP.md) beforehand to find out when deployment is required.

To deploy SmartNS, run `smartns_dpu`, then load the Linux kernel module, and finally run user applications, **in this order**.

### 3.1 Run `smartns_dpu` on Arm processors (`BF1` and `BF2`)

On `BF2`:

~~~bash
cd ~/nfs/SmartNS/build_dpu
sudo ./smartns_dpu -deviceName mlx5_2 -is_server
~~~

On `BF1`:

~~~bash
cd ~/nfs/SmartNS/build_dpu
sudo ./smartns_dpu -deviceName mlx5_2
~~~

### 3.2 Load Linux kernel module (`Host1` and `Host2`)

On `Host1` and `Host2`:

~~~bash
cd ~/nfs/SmartNS/kernel
./insmod.sh
~~~

### 3.3 Run a basic SmartNS data-path test (`Host1` and `Host2`)

On `Host2` (server):

~~~bash
cd ~/nfs/SmartNS/build_host
./write_bw -deviceName mlx5_0 -batch_size 1 -threads 1 -outstanding 32 -payload_size 1024 -is_server -iterations 1
~~~

On `Host1` (client):

~~~bash
cd ~/nfs/SmartNS/build_host
./write_bw -deviceName mlx5_0 -batch_size 1 -threads 1 -outstanding 32 -payload_size 1024 -serverIp 10.0.0.200 -iterations 1
~~~

The server does not stop automatically. Use `CTRL+C` to stop the server on `Host2`.

Then use `sudo rmmod smartns` to remove the kernel module after client and server finish.

Finally, use `CTRL+C` to stop `smartns_dpu` after removing the `smartns` kernel module.

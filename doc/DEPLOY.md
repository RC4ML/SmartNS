# Connect and Deploy SmartNS

We provide two machines for AE reviewers. Each machine is equipped with an NVIDIA BlueField-3 B3220 400GbE NIC and connected back-to-back using two 200GbE QSFP56 cables. The topology is shown in the figure below. We use **`Host1` `BF1` `Host2` `BF2`** as labels in the deployment instructions.

![machine status](./image/machine.png)

Applications and the kernel module run on hosts, while `smartns_dpu` (the core program) runs on the BlueField-3 Arm processors.

**All prerequisites in `INSTALL.md` have already been completed** on our artifact machines. Reviewers can start directly from the final build step if needed.

## 1. Connect to artifact machines

**Important:** both USER and PASSWORD are `eurosys26`.

We have created a sudo user `eurosys26` on all machines and disabled password-based SSH login for security reasons. Use the following process to connect:

1. Download the private key `eurosys26_id_ed25519` from the submission website.
2. Get the jump server domain name (referred to as `jump` below) from the submission website. We provide both IPv4 and IPv6 servers (IPv6 recommended).
3. `ssh eurosys26@js.v4.rc4ml.org -p xxx -i eurosys26_id_ed25519`
4. Start testing.
5. If you have any questions, please contact us.

You can find an NFS folder named `nfs`. It is located on `Host1` and shared with `Host2`, `BF1`, and `BF2`, so cloning SmartNS into this folder enables a synchronized workflow across machines.

## 2. Load basic RDMA kernel modules (must check after each reboot)
Execute following commands on `Host1` and `Host2`:
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

## 3. Verify link status (must check after each reboot)

To ensure link aggregation works correctly, log in to `BF1` and `BF2` and run:

~~~bash
ifconfig | grep bond
~~~

If you do not see output similar to `bond0: flags=5187<UP,BROADCAST,RUNNING,MASTER,MULTICAST>`, execute the following commands on `BF1` and `BF2` (required after each reboot):

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

After bonding, verify connectivity with ping to `10.0.0.100`, `10.0.0.101`, `10.0.0.200`, and `10.0.0.201`.

## 4. Set MTU to 9000 (must check after each reboot)

Please set MTU to 9000 on all relevant interfaces.

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

You can run `ping -s 8192 10.0.0.200` on `Host1` to verify MTU settings.

## 4.5 Use all in one scripts

We provide a `all-in-one` scripts in scripts folders, you can directly run like `bash ./scripts/deploy_host1.sh` on **Host1** and `bash ./scripts/deploy_bf1.sh` on **BF1**.

## 5. Build SmartNS

SmartNS includes three components:

- `smartns_dpu` (runs on Arm processors)
- Linux kernel module (runs on hosts)
- User-layer applications (run on both hosts and Arm)

~~~bash
cd nfs
git clone --recursive https://github.com/RC4ML/SmartNS
cd SmartNS

# execute on host
mkdir build_host
cd build_host
cmake ..
make -j # build test code and lib

cd ../kernel
make -j # build kernel module

# execute on ARM! (BF1 and BF2)
mkdir build_dpu
cd build_dpu
cmake ..
make -j # build smartns_dpu and test code
~~~

## 6. Deploy SmartNS

To deploy SmartNS, run `smartns_dpu`, then load the Linux kernel module, and finally run user applications, **in this order**.

### 6.1 Run `smartns_dpu` on Arm processors (`BF1` and `BF2`)

On `BF2`:

~~~bash
sudo ./smartns_dpu -deviceName mlx5_2 -is_server
~~~

On `BF1`:

~~~bash
sudo ./smartns_dpu -deviceName mlx5_2
~~~

### 6.2 Load Linux kernel module (`Host1` and `Host2`)

On `Host1` and `Host2`:

~~~bash
cd kernel
./insmod.sh
~~~

### 6.3 Run basic test (`Host1` and `Host2`)

On `Host2` (server):

~~~bash
./write_bw -deviceName mlx5_0 -batch_size 1 -threads 1 -outstanding 32 -payload_size 1024 -is_server -iterations 1
~~~

On `Host1` (client):

~~~bash
./write_bw -deviceName mlx5_0 -batch_size 1 -threads 1 -outstanding 32 -payload_size 1024 -serverIp 10.0.0.200 -iterations 1
~~~

Please note that the server does not stop automatically. Use `CTRL+C` to stop the server on `Host2`.

Then use `sudo rmmod smartns` to remove the kernel module after client and server finish.

Finally, use `CTRL+C` to stop `smartns_dpu` after removing the `smartns` kernel module.

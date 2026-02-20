# Evaluation

**Important:** All dependencies listed in [INSTALL.md](./INSTALL.md) are already installed on our artifact machines.

**Important:** Please refer to [DEPLOY.md](./DEPLOY.md) for connecting to the artifact machines and deploying SmartNS on BlueField-3.

**Important:** Please strictly follow the specified execution order and machine assignment in each experiment; otherwise, unexpected behavior may occur.

**Important:** Since this project involves Linux kernel modules, kernel freeze/hang/illegal access can occur. Please use `ipmitool` for power reset when needed, and then refer to [DEPLOY.md](./DEPLOY.md) to restore the environment after reboot.

**Important:** Sometime the experiment result will significantly reduced, which is mainly becase of the unknown DMA engine question, please use `ipmitool` to restart the machine.

## 0. Ensure link status

Use the following command on `Host1` to check reachability:

~~~bash
# Host1
ping -s 8192 10.0.0.200
~~~

Use `perftest` to verify link bandwidth:

~~~bash
# on Host1
cd ~/nfs
git clone https://github.com/linux-rdma/perftest
cd perftest && ./autogen.sh && ./configure
make -j

# on Host2
./ib_send_bw -d mlx5_0 -x 3 -q 8 -s 1048576 --run_infinitely

# on Host1
./ib_send_bw -d mlx5_0 -x 3 -q 8 -s 1048576 --run_infinitely 10.0.0.200
~~~

If everything is correct, you should see an average bandwidth around 46750 MB/s (about 375Gbps).

## 1. Header-only Offloading TX Path

This evaluation corresponds to Figure 11 in the paper. In this experiment, you do **not** need to run `smartns_dpu` or load the kernel module.

### 1.1 Run RDMA-assisted TX

First, on **BF2**, run:

~~~bash
sudo ./arm_relay_1_1 -deviceName mlx5_2 -batch_size 1 -outstanding 32 -nodeType 2 -threads 2 -payload_size 1024
~~~

Then, on **BF1**, run:

~~~bash
sudo ./arm_relay_1_1 -deviceName mlx5_2 -batch_size 1 -outstanding 32 -nodeType 1 -threads 2  -iterations 5000 -payload_size 1024
~~~

Finally, on **Host1**, run:

~~~bash
sudo ./arm_relay_1_1 -deviceName mlx5_0 -batch_size 1 -outstanding 32 -nodeType 0 -threads 2 -payload_size 1024 -serverIp 10.0.0.101
~~~

You will see output on `BF1` similar to the following. Sum the bandwidth of `thread0` and `thread1` as the final result.

~~~bash
thread [1], duration [2.842365]s, throughput [18.025908] Gbps
thread [0], duration [3.154976]s, throughput [17.767303] Gbps
~~~

You can change `payload_size` from 1024 to 8192 to evaluate different payload sizes.

After `BF1` finishes, you can stop applications on `Host1` and `BF2` with `CTRL+C` (these programs are hardcoded with spin loops).

### 1.2 Run DMA-assisted TX

Change `arm_relay_1_1` to `arm_relay_1_2`; other settings are identical to Section 1.1.

### 1.3 Run Header-only Offloading TX Path

Change `arm_relay_1_2` to `arm_relay_1_3`.

### 1.4 Check Arm memory bandwidth

Use `scripts/bf3_memory_bw.py`. While running the above experiments, execute `python3 ./scripts/bf3_memory_bw.py` on **BF1** to monitor Arm memory bandwidth.

This script outputs average Arm memory bandwidth at one-second intervals.

## 2. Unlimited-working-set In-Cache Processing RX Path

This evaluation corresponds to Figure 13 in the paper. Please note that the application placement and launch policy differ from Experiment 1.

### 2.1 Run Unlimited-working-set In-Cache Processing RX Path

First, on **BF2**, run:

~~~bash
sudo ./arm_relay_2_3 -deviceName mlx5_2 -batch_size 1 -outstanding 32 -nodeType 1 -threads 8 -payload_size 8192
~~~

Then, on **Host2**, run:

~~~bash
sudo ./arm_relay_2_3 -deviceName mlx5_0 -batch_size 1 -outstanding 32 -nodeType 2 -threads 8  -serverIp 10.0.0.201 -payload_size 8192
~~~

Finally, on **BF1**, run:

~~~bash
sudo ./arm_relay_2_3 -deviceName mlx5_2 -batch_size 1 -outstanding 32 -nodeType 0 -threads 8  -payload_size 8192
~~~

You will see output on `BF2` similar to the following. Sum all thread bandwidth values as the final result.

~~~bash
thread [2], duration [2.632949]s, recv speed [50.976189] Gbps
thread [3], duration [2.660956]s, recv speed [50.439698] Gbps
thread [4], duration [2.939568]s, recv speed [45.659001] Gbps
thread [6], duration [2.939651]s, recv speed [45.657707] Gbps
thread [7], duration [2.951377]s, recv speed [45.476313] Gbps
thread [5], duration [2.951522]s, recv speed [45.474098] Gbps
thread [1], duration [2.952497]s, recv speed [45.459082] Gbps
thread [0], duration [2.994036]s, recv speed [44.828368] Gbps
~~~

You can change `payload_size` from 512 to 8192 to evaluate different payload sizes.

After `BF2` finishes, you can stop applications on `Host2` and `BF1` with `CTRL+C` (these programs are hardcoded with spin loops).

### 2.2 RDMA-assisted RX

Change `arm_relay_2_3` to `arm_relay_2_1`.

### 2.3 DMA-assisted RX

Change `arm_relay_2_1` to `arm_relay_2_2`.

### 2.4 Check Arm LLC bandwidth

Use `scripts/bf3_llc_bw.py`. While running the above experiments, execute `python3 ./scripts/bf3_llc_bw.py` on **BF2** to monitor Arm LLC bandwidth.

This script outputs average Arm LLC bandwidth at one-second intervals.

## 3. Comparison with other network stacks

This evaluation corresponds to Figure 10 in the paper.

### 3.1 SNAP baseline

On `Host2`:

~~~bash
sudo ./snap_bench -is_server  -iterations 1000 -numPack 51200 -payload_size 2048  -threads 1
~~~

Then on `Host1`:

~~~bash
sudo ./snap_bench -serverIp 10.0.0.200  -iterations 1000 -numPack 51200 -payload_size 2048 -threads 1
~~~

After completion, you should see output similar to:

~~~bash
26:099551 INFOR: Thread [ 0] has been moved to core [ 0]
Data verification success, thread [0], duration [23.126377]s, throughput [36.272902] GpbsTotal bandwidth: 36.272902 Gbps
~~~

You can change `threads` to vary the number of connections.

### 3.2 RDMA baseline

Due to limitations, RDMA baseline code is maintained in another repository: https://github.com/carlzhang4/libr

Please execute the following commands on `Host1`:

~~~bash
git clone --recursive https://github.com/carlzhang4/libr
cd libr
mkdir build_host
cd build_host && cmake ..
make -j
~~~

Then on `Host2`, run:

~~~bash
sudo ./rdma_bench -nodeId 0 -serverIp 10.0.0.200 -iterations 500 -packSize 2048 -numPack 51200 -threads 1
~~~

Finally, on `Host1`, run:

~~~bash
sudo ./rdma_bench -nodeId 1 -serverIp 10.0.0.200 -iterations 500 -packSize 2048 -numPack 51200 -threads 1
~~~

You can change `threads` to vary the number of connections.

### 3.3 SmartNS

Please refer to Section 6 in [DEPLOY.md](./DEPLOY.md), follow the execution order (run `smartns_dpu` and load the Linux kernel module), and use the following host commands:

~~~bash
## Host2(Server)
./write_bw -deviceName mlx5_0 -batch_size 1 -outstanding 80 -payload_size 2048 -is_server -iterations 10000 -threads 1

## Host1(Client)
./write_bw -deviceName mlx5_0 -batch_size 1 -outstanding 80 -payload_size 2048 -serverIp 10.0.0.200 -iterations 10000 -threads 1
~~~

You can change `threads` to vary the number of connections.

### 3.4 Check host memory bandwidth

You can check host memory bandwidth by running:

~~~bash
sudo pcm-memory
~~~

## 4. Solar application

This evaluation corresponds to Figure 16 in the paper.

On `Host2`, run:

~~~bash
sudo ./solar_bench -is_server  -iterations 100 -numPack 51200 -payload_size 4096 -threads 1  -type 0
~~~

On `Host1`, run:

~~~bash
sudo ./solar_bench -serverIp 10.0.0.200  -iterations 100 -numPack 51200 -payload_size 4096  -threads 1  -type 0
~~~

You should see output similar to:

~~~bash
Total Speed: 5.030016 Mops
~~~

You can change different `type` and `threads` settings for additional data points.

|                           | Type |
| ------------------------- | ---- |
| CPU-only                  | 0    |
| CPU+w/ CRC offload        | 1    |
| CPU+w/ CRC offload+w/ DSA | 2    |

## 5. Automation Quick Start (EXP1/EXP2/EXP4)

This section provides ready-to-run commands for reviewers.
All automation scripts are located in `test/automation/`.
Default SSH settings in scripts: user `eurosys26`, key `/path/to/SmartNS/eurosys26_id_ed25519`, port `22`, and machine IPs `10.130.142.26/27/40/41` for `Host1/Host2/BF1/BF2`.
Please note that id_ed25519 key need `chmod 600 eurosys26_id_ed25519` to set correct permission!

### 5.1 Prerequisites

Run the following from `Host1`:

1. Ensure the environment in [DEPLOY.md](./DEPLOY.md) is ready (link/MTU/module checks).
2. Ensure SSH can reach `10.130.142.27` (`Host2`), `10.130.142.40` (`BF1`), and `10.130.142.41` (`BF2`) from `Host1` (`10.130.142.26`) with the private key from submission.
3. Ensure SmartNS is built on both Host and DPU sides:

~~~bash
cd ~/nfs/SmartNS

# on host
cd build_host
cmake ..
make -j

# on Arm
cd build_dpu
cmake ..
make -j
~~~

### 5.2 EXP1 automation + plotting

`exp1_run_auto.py` automatically sweeps:
- methods: `arm_relay_1_1`, `arm_relay_1_2`, `arm_relay_1_3`
- `payload_size`: `64,128,256,512,1024,2048,4096,8192`

~~~bash
cd ~/nfs/SmartNS
python3 ./test/automation/exp1_run_auto.py
~~~

If your key is stored in a different path, add `--ssh-key /path/to/key`.

~~~bash
cd ~/nfs/SmartNS
python3 ./test/automation/exp1_plot.py \
  --input test/results/exp1_results.csv \
  --output test/results/exp1_figure.svg
~~~

### 5.3 EXP2 automation + plotting

`exp2_run_auto.py` automatically sweeps:
- methods: `arm_relay_2_1`, `arm_relay_2_2`, `arm_relay_2_3`
- `payload_size`: `512,1024,2048,4096,8192`

~~~bash
cd ~/nfs/SmartNS
python3 ./test/automation/exp2_run_auto.py
~~~

If your key is stored in a different path, add `--ssh-key /path/to/key`.

~~~bash
cd ~/nfs/SmartNS
python3 ./test/automation/exp2_plot.py \
  --input test/results/exp2_results.csv \
  --output test/results/exp2_figure.svg
~~~

### 5.4 EXP4 automation + plotting

`exp4_run_auto.py` automatically sweeps:
- `threads`: `1..12`
- `type`: `0,1,2`

~~~bash
cd ~/nfs/SmartNS
python3 ./test/automation/exp4_run_auto.py
~~~

If your key is stored in a different path, add `--ssh-key /path/to/key`.

~~~bash
cd ~/nfs/SmartNS
python3 ./test/automation/exp4_plot.py \
  --input test/results/exp4_results.csv \
  --output test/results/exp4_figure.svg
~~~

### 5.5 Output locations

All scripts write outputs under `test/results/`:
- CSV data files: `exp1_results.csv`, `exp2_results.csv`, `exp4_results.csv`
- Run logs: `exp1_logs/`, `exp2_logs/`, `exp4_logs/`
- Figures: `exp1_figure.svg`, `exp2_figure.svg`, `exp4_figure.svg`

### 5.6 Auto-stop behavior

To avoid hangs in multi-round automation:
- The relay/client/server test binaries for EXP1 and EXP2 now accept `-auto_exit_sec` (default `0`, disabled).
- Automation scripts set `-auto_exit_sec` for non-key roles, and send `SIGINT`/`SIGTERM`/`SIGKILL` automatically after the key role finishes.
- This guarantees each round can proceed to the next payload size/thread configuration without manual `CTRL+C`.

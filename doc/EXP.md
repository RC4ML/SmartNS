# Evaluation

**Important:** All dependencies listed in [INSTALL.md](./INSTALL.md) are already installed on our artifact machines.

**Important:** Please complete host setup in [DEPLOY.md](./DEPLOY.md) before running experiments.

**Important:** Please strictly follow the specified execution order and machine assignment in each experiment; otherwise, unexpected behavior may occur.

**Important:** Since this project involves Linux kernel modules, kernel freeze/hang/illegal access can occur. Please use `ipmitool` for power reset when needed, and then refer to [DEPLOY.md](./DEPLOY.md) to restore the environment after reboot.

**Important:** Sometimes results may significantly drop due to unstable DMA behavior. In this case, reboot the machines with `ipmitool` and rerun the configuration steps in [DEPLOY.md](./DEPLOY.md).

## 0. Global notes

### 0.1 Method-name mapping between artifact binaries and paper terms

| Binary | Paper term | Experiment |
| ------ | ---------- | ---------- |
| `tx_rdma_assisted` | RDMA-assisted TX | EXP1 (Figure 11) |
| `tx_dma_assisted` | DMA-assisted TX | EXP1 (Figure 11) |
| `tx_header_only_offloading` | Header-only Offloading TX | EXP1 (Figure 11) |
| `rx_rdma_assisted` | RDMA-assisted RX | EXP2 (Figure 13) |
| `rx_dma_assisted` | DMA-assisted RX | EXP2 (Figure 13) |
| `rx_unlimited_working_set_in_cache` | Unlimited-working-set In-Cache RX | EXP2 (Figure 13) |


### 0.2 Link sanity check (recommended before experiments)

Use the following command on `Host1` to check MTU/reachability:

~~~bash
ping -s 8192 10.0.0.200
~~~

Use `perftest` to verify raw link bandwidth:

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

If everything is correct, average bandwidth should be around 46750 MB/s (about 375Gbps).

## 1. Header-only Offloading TX Path

This evaluation corresponds to Figure 11 in the paper.
For this experiment, you do **not** need to run `smartns_dpu` or load the kernel module.

### 1.1 Quick run (automation, recommended)

`exp1_run_auto.py` sweeps:

- methods: `tx_rdma_assisted`, `tx_dma_assisted`, `tx_header_only_offloading`
- `payload_size`: `128,256,512,1024,2048,4096,8192`

~~~bash
cd ~/nfs/SmartNS
python3 ./test/automation/exp1_run_auto.py
~~~

If your key is stored in a different path:

~~~bash
cd ~/nfs/SmartNS
python3 ./test/automation/exp1_run_auto.py --ssh-key /path/to/key
~~~

`exp1_run_auto.py` collects throughput and memory-bandwidth data into `test/results/exp1_results.csv`.
It does not generate figures directly.

Run `exp1_plot.py` once to generate both figures:

- Figure 11.a style: payload size vs throughput (`test/results/exp1_figure_a.svg`)
- Figure 11.b style: throughput vs Arm memory bandwidth (`test/results/exp1_figure_b.svg`)

~~~bash
cd ~/nfs/SmartNS
python3 ./test/automation/exp1_plot.py \
  --input test/results/exp1_results.csv \
  --output test/results/exp1_figure.svg
~~~

### 1.2 Manual commands (alternative to automation)

#### 1.2.1 RDMA-assisted TX (`tx_rdma_assisted`)

First, on **BF2**, run:

~~~bash
sudo ./tx_rdma_assisted -deviceName mlx5_2 -batch_size 1 -outstanding 32 -nodeType 2 -threads 2 -payload_size 1024
~~~

Then, on **BF1**, run:

~~~bash
sudo ./tx_rdma_assisted -deviceName mlx5_2 -batch_size 1 -outstanding 32 -nodeType 1 -threads 2 -iterations 5000 -payload_size 1024
~~~

Finally, on **Host1**, run:

~~~bash
sudo ./tx_rdma_assisted -deviceName mlx5_0 -batch_size 1 -outstanding 32 -nodeType 0 -threads 2 -payload_size 1024 -serverIp 10.0.0.101
~~~

You will see output on `BF1` similar to the following. Sum thread bandwidth as final result:

~~~bash
thread [1], duration [2.842365]s, throughput [18.025908] Gbps
thread [0], duration [3.154976]s, throughput [17.767303] Gbps
~~~

#### 1.2.2 DMA-assisted TX (`tx_dma_assisted`)

Change `tx_rdma_assisted` to `tx_dma_assisted`; other settings are identical to Section 1.2.1.

#### 1.2.3 Header-only Offloading TX (`tx_header_only_offloading`)

Change `tx_dma_assisted` to `tx_header_only_offloading`.

#### 1.2.4 Optional: manual Arm memory-bandwidth monitoring for Figure 11.b

While running EXP1, execute the monitor on `BF1`:

~~~bash
python3 ./scripts/bf3_memory_bw.py
~~~

After `BF1` finishes, stop applications on `Host1` and `BF2` with `CTRL+C`.

## 2. Unlimited-working-set In-Cache Processing RX Path

This evaluation corresponds to Figure 13 in the paper.
Please note that application placement and launch order differ from EXP1.

### 2.1 Quick run (automation, recommended)

`exp2_run_auto.py` sweeps:

- methods: `rx_rdma_assisted`, `rx_dma_assisted`, `rx_unlimited_working_set_in_cache`
- fixed parameters:
  - `payload_size=8192`
  - `pkt_buf_size=8192`
  - `pkt_handle_batch=1`
  - `threads=12`
- `NB_RXD/NB_TXD`: `64,96,128,160,192,224,256,384,512`
- working-set size formula:
  - `working_set_size_bytes = NB_RXD * payload_size * threads`
  - `working_set_size_mib = working_set_size_bytes / 1024 / 1024`

~~~bash
cd ~/nfs/SmartNS
python3 ./test/automation/exp2_run_auto.py
~~~

If your key is stored in a different path:

~~~bash
cd ~/nfs/SmartNS
python3 ./test/automation/exp2_run_auto.py --ssh-key /path/to/key
~~~

`exp2_run_auto.py` collects throughput and memory-bandwidth data into `test/results/exp2_results.csv`.
The CSV includes `nb_rxd`, `nb_txd`, and `working_set_size` for plotting.
It does not generate figures directly.

Run `exp2_plot.py` once to generate both figures:

- Figure 13.a style: working-set size (MiB) vs throughput (`test/results/exp2_figure_a.svg`)
- Figure 13.b style: working-set size (MiB) vs Arm memory bandwidth (`test/results/exp2_figure_b.svg`)

~~~bash
cd ~/nfs/SmartNS
python3 ./test/automation/exp2_plot.py \
  --input test/results/exp2_results.csv \
  --output test/results/exp2_figure.svg
~~~

### 2.2 Manual commands (alternative to automation)

#### 2.2.1 Unlimited-working-set In-Cache RX (`rx_unlimited_working_set_in_cache`)

First, on **BF2**, run:

~~~bash
sudo ./rx_unlimited_working_set_in_cache -deviceName mlx5_2 -batch_size 1 -outstanding 32 -nodeType 1 -threads 12 -payload_size 8192 -nb_rxd 64 -nb_txd 64 -pkt_buf_size 8192 -pkt_handle_batch 1
~~~

Then, on **Host2**, run:

~~~bash
sudo ./rx_unlimited_working_set_in_cache -deviceName mlx5_0 -batch_size 1 -outstanding 32 -nodeType 2 -threads 12 -serverIp 10.0.0.201 -payload_size 8192 -nb_rxd 64 -nb_txd 64 -pkt_buf_size 8192 -pkt_handle_batch 1
~~~

Finally, on **BF1**, run:

~~~bash
sudo ./rx_unlimited_working_set_in_cache -deviceName mlx5_2 -batch_size 1 -outstanding 32 -nodeType 0 -threads 12 -payload_size 8192 -nb_rxd 64 -nb_txd 64 -pkt_buf_size 8192 -pkt_handle_batch 1
~~~

You will see output on `BF2` similar to the following:

~~~bash
thread [0], duration [2.994036]s, recv speed [44.828368] Gbps
thread [1], duration [2.952497]s, recv speed [45.459082] Gbps
...
RESULT|experiment=2|method=rx_unlimited_working_set_in_cache|payload_size=8192|threads=12|total_gbps=540.123456
~~~

For manual sweep, keep all other parameters fixed and iterate:

- `NB_RXD/NB_TXD = 64,96,128,160,192,224,256,384,512`

#### 2.2.2 RDMA-assisted RX (`rx_rdma_assisted`)

Change `rx_unlimited_working_set_in_cache` to `rx_rdma_assisted`.

#### 2.2.3 DMA-assisted RX (`rx_dma_assisted`)

Change `rx_rdma_assisted` to `rx_dma_assisted`.

#### 2.2.4 Optional: manual Arm memory-bandwidth monitoring for Figure 13.b

While running EXP2, execute the monitor on `BF2`:

~~~bash
python3 ./scripts/bf3_memory_bw.py
~~~

After `BF2` finishes, stop applications on `Host2` and `BF1` with `CTRL+C`.

## 3. Comparison with other network stacks

This evaluation corresponds to Figure 10 in the paper.

### 3.1 SNAP baseline

On `Host2`:

~~~bash
sudo ./snap_bench -is_server -iterations 1000 -numPack 51200 -payload_size 2048 -threads 1
~~~

Then on `Host1`:

~~~bash
sudo ./snap_bench -serverIp 10.0.0.200 -iterations 1000 -numPack 51200 -payload_size 2048 -threads 1
~~~

After completion, you should see output similar to:

~~~bash
26:099551 INFOR: Thread [ 0] has been moved to core [ 0]
Data verification success, thread [0], duration [23.126377]s, throughput [36.272902] GpbsTotal bandwidth: 36.272902 Gbps
~~~

### 3.2 RDMA baseline

Due to limitations, RDMA baseline code is maintained in another repository: <https://github.com/carlzhang4/libr>

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

### 3.3 SmartNS

For this baseline, deployment is required.
Follow Section 3 in [DEPLOY.md](./DEPLOY.md), then use:

~~~bash
# Host2 (server)
./write_bw -deviceName mlx5_0 -batch_size 1 -outstanding 80 -payload_size 2048 -is_server -iterations 10000 -threads 1

# Host1 (client)
./write_bw -deviceName mlx5_0 -batch_size 1 -outstanding 80 -payload_size 2048 -serverIp 10.0.0.200 -iterations 10000 -threads 1
~~~

### 3.4 Optional: host memory bandwidth check

~~~bash
sudo pcm-memory
~~~

## 4. Solar application

This evaluation corresponds to Figure 16 in the paper.

### 4.1 Quick run (automation, recommended)

`exp4_run_auto.py` sweeps:

- `threads`: `1..12`
- `type`: `0,1,2`

~~~bash
cd ~/nfs/SmartNS
python3 ./test/automation/exp4_run_auto.py
~~~

If your key is stored in a different path:

~~~bash
cd ~/nfs/SmartNS
python3 ./test/automation/exp4_run_auto.py --ssh-key /path/to/key
~~~

Then generate the figure:

~~~bash
cd ~/nfs/SmartNS
python3 ./test/automation/exp4_plot.py \
  --input test/results/exp4_results.csv \
  --output test/results/exp4_figure.svg
~~~

### 4.2 Manual commands (alternative to automation)

On `Host2`, run:

~~~bash
sudo ./solar_bench -is_server -iterations 100 -numPack 51200 -payload_size 4096 -threads 1 -type 0
~~~

On `Host1`, run:

~~~bash
sudo ./solar_bench -serverIp 10.0.0.200 -iterations 100 -numPack 51200 -payload_size 4096 -threads 1 -type 0
~~~

You should see output similar to:

~~~bash
Total Speed: 5.030016 Mops
~~~

| Method | `type` |
| ------ | ------ |
| CPU-only | `0` |
| CPU + CRC offload | `1` |
| CPU + CRC offload + DSA | `2` |

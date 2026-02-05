# Evaluation

**Important:** All dependencies located in [INSTALL.md](./INSTALL.md) are installed on our artifact machine. 

**Important:** Please see [DEPLOY.md](./DEPLOY.md) for connecting to our artifact machine and deploying SmartNS on BlueField-3.

**Important:** Please carefully follow the specified execution order and the corresponding machine during the experiment; otherwise, unexpected situations may occur.

**Important:** Since this project involves the linux kernel module, kernel freezing / hang / illegal access is normal behavior. Please use ipmitools to power reset. Please refer to [DEPLOY.md](./DEPLOY.md) to prepare the environment after each machine restart.

## 0. Ensure link status

We use following commands on Host1 to check link reachable:
~~~bash
# Host1
ping -s 8192 10.0.0.200
~~~ 

We use perftest to check link bandwidth:
~~~bash
git clone https://github.com/linux-rdma/perftest
cd perftest && ./autogen.sh && ./configure
make -j

# on Host2
./ib_send_bw -d mlx5_0 -x 3 -q 8 -s 1048576 --run_infinitely

# on Host1
./ib_send_bw -d mlx5_0 -x 3 -q 8 -s 1048576 --run_infinitely 10.0.0.200
~~~

If everything is OK, you will see BW average about 46750 MB/s (about 375Gbps)

## 1.  Header-only Offloading TX Path

This is the evalution for fig.11 in the paper. Don't need run smartns_dpu and kernel module in this exp.

### 1.1 Run the RDMA-assisted TX

At first on **BF2**, run following command:
~~~bash
sudo ./arm_relay_1_1 -deviceName mlx5_2 -batch_size 1 -outstanding 32 -nodeType 2 -threads 2 -payload_size 1024
~~~

Then on **BF1**, run following command:
~~~bash
sudo ./arm_relay_1_1 -deviceName mlx5_2 -batch_size 1 -outstanding 32 -nodeType 1 -threads 2  -iterations 5000 -payload_size 1024
~~~

At last on **Host1**, run following comamnd:
~~~bash
sudo ./arm_relay_1_1 -deviceName mlx5_0 -batch_size 1 -outstanding 32 -nodeType 0 -threads 2 -payload_size 1024 -serverIp 10.0.0.101
~~~

You will see the output located on BF1, like following, then combine thread0 and thread1 bandwidth is the final result.
~~~bash
thread [1], duration [2.842365]s, throughput [18.025908] Gbps
thread [0], duration [3.154976]s, throughput [17.767303] Gbps
~~~

You can change `payload_size` from 1024 to 8192 to view the result of different payloads.

After BF1 finish, you can use CTRL+C for Host1 and BF2 application, they are hardcode as a spin loop :) .

### 1.2 Run the DMA-assisted TX

just change `arm_relay_1_1` to `arm_relay_1_2`, others totally same as 1.1

### 1.3 Run Header-only Offloading TX Path

just change `arm_relay_1_2` to `arm_relay_1_3`.

### 1.4 Check Arm memory bandwidth

You can see a python script named scripts/bf3_memory_bw.py, when running above experiments, you can use `python3 ./scripts/bf3_memory_bw.py` on **BF1** to check the Arm bandwidth.

This script will output the average Arm memory bandwidth at one-second intervals.

## 2. Unlimited-working-set In-Cache Processing RX Path

This is the evalution for fig.13 in the paper, please note that application have different location and different launch policy compared with exp1.

### 2.1 Run the Unlimited-working-set In-Cache Processing RX Path
At first on **BF2**, run following command:
~~~bash
sudo ./arm_relay_2_3 -deviceName mlx5_2 -batch_size 1 -outstanding 32 -nodeType 1 -threads 8 -payload_size 8192
~~~

Then at **Host2**, run following command:
~~~bash
sudo ./arm_relay_2_3 -deviceName mlx5_0 -batch_size 1 -outstanding 32 -nodeType 2 -threads 8  -serverIp 10.0.0.201 -payload_size 8192
~~~

At last at **BF1**, run following command:
~~~bash
sudo ./arm_relay_2_3 -deviceName mlx5_2 -batch_size 1 -outstanding 32 -nodeType 0 -threads 8  -payload_size 8192
~~~

You will see the output located on BF2, like following, then combine all threads bandwidth is the final result.
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

You can change `payload_size` from 1024 to 8192 to view the result of different payloads.

After BF2 finish, you can use CTRL+C for Host2 and BF1 application, they are hardcode as a spin loop .

### 2.2 RDMA-assisted RX

just change `arm_relay_2_3` to `arm_relay_2_1`.

### 2.3 DMA-assisted RX

just change `arm_relay_2_1` to `arm_relay_2_2`.

### 2.4 Check Arm LLC bandwidth

You can see a python script named scripts/bf3_llc_bw.py, when running above experiments, you can use `python3 ./scripts/bf3_llc_bw.py` on **BF2** to check the Arm LLC bandwidth.

This script will output the average Arm LLC bandwidth at one-second intervals.


## 3. Comparison with other Network Stacks

This is the evalution for fig.10 in the paper.

### 3.1 Snap baseline

On Host2:
~~~
sudo ./snap_bench -is_server  -iterations 1000 -numPack 51200 -payload_size 2048  -threads 1 
~~~

Then on Host1:
~~~
sudo ./snap_bench -serverIp 10.0.0.200  -iterations 1000 -numPack 51200 -payload_size 2048 -threads 1
~~~

After finish, you can see the result like following, and you can change `threads` args to change connection number:
~~~bash
26:099551 INFOR: Thread [ 0] has been moved to core [ 0]
Data verification success, thread [0], duration [23.126377]s, throughput [36.272902] GpbsTotal bandwidth: 36.272902 Gbps
~~~


### 3.2 RDMA baseline

Due to some limitation, we left RDMA baseline code in another repo https://github.com/carlzhang4/libr, please exec following commands on Host1

~~~bash
git clone --recursive https://github.com/carlzhang4/libr
cd libr
mkdir build_host
cd build_host && cmake ..
make -j
~~~

Then, on Host2, exec following command:
~~~bash
sudo ./rdma_bench -nodeId 0 -serverIp 10.0.0.200 -iterations 500 -packSize 2048 -numPack 51200 -threads 1
~~~

At last, one Host1, exec following command:
~~~bash
sudo ./rdma_bench -nodeId 1 -serverIp 10.0.0.200 -iterations 500 -packSize 2048 -numPack 51200 -threads 1
~~~

You can change `threads` args to change connection number

### 3.3 SmartNS

Please refer section 6 Deploy SmartNS in [DEPLOY.md](./DEPLOY.md), follow the exec order(run smartns_dpu and load linux kernel module) and change the host commands to:

~~~bash
## Host2(Server)
./write_bw -deviceName mlx5_0 -batch_size 1 -outstanding 80 -payload_size 2048 -is_server -iterations 10000 -threads 1

## Host1(Client)
./write_bw -deviceName mlx5_0 -batch_size 1 -outstanding 80 -payload_size 2048 -serverIp 10.0.0.200 -iterations 10000 -threads 1
~~~
You can change `threads` args to change connection number

### 3.4 Check Host memory bandwidth

You can simply check host memory bandwidth by
~~~bash
sudo pcm-memory
~~~

## 4. Solar application

This is the evalution for fig.16 in the paper.

On Host2, exec following command:
~~~bash
sudo ./solar_bench -is_server  -iterations 100 -numPack 51200 -payload_size 4096 -threads 1  -type 0
~~~

On Host1, exec following:
~~~bash
sudo ./solar_bench -serverIp 10.0.0.200  -iterations 100 -numPack 51200 -payload_size 4096  -threads 1  -type 0
~~~

You will see the result like following:
~~~bash
Total Speed: 5.030016 Mops
~~~

You can change differnet type and threads for more data points.

|                           | Type |
| ------------------------- | ---- |
| CPU-only                  | 0    |
| CPU+w/ CRC offload        | 1    |
| CPU+w/ CRC offload+w/ DSA | 2    |




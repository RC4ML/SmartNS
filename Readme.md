# SmartNS: Enabling Line-rate and Flexible Network Stack with SmartNIC [EuroSys26]

A SmartNIC-centric network stack with software transport programmability and 400Gbps line-rate packet processing capabilities.


## Required hardware and software

- 2 x BlueField-3 SmartNIC with 400Gbps ethernet link
- Required lib: cmake, gflags, numa, pthread
- HugePage: At least 2048 huge pages on each NUMA node
- g++ >= 11.3.0
- MLNX_OFED 
- Intel Sapphire Rapids CPU or Emerald Rapids CPU with Data Streaming Accelerator(DSA) equipped (optimal)


## Install Dependencies and Build
See [INSTALL.md](./doc/INSTALL.md) for install dependencies and build SmartNS on a BlueField-3 equipped machine.

## Connect and Deploy SmartNS
See [DEPLOY.md](./doc/DEPLOY.md) for connecting to our artifact machine and  deploying SmartNS on BlueField-3.

## Run Test
If Check if the configuration is correct in Run Experiments of [EXP.md](./doc/EXP.md) passes, then everything will be fine. Please refer to exp.md for more details.

## Directory Structure

~~~
.
├── doc
├── include
│   ├── devx
│   ├── dma
│   ├── fw
│   ├── raw_packet
│   ├── rdma_cm
│   ├── rxe
│   └── tcp_cm
├── kernel (smartns kernel module code)
├── lib (smartns lib for uplayer application)
├── scripts (bf3 monitor python script)
├── src
│   ├── devx
│   ├── dma
│   ├── dpu
│   ├── raw_packet
│   ├── rdma_cm
│   ├── rxe
│   └── tcp_cm
├── test (test code)
├── third_party
│   ├── atomic_queue
│   ├── HdrHistogram_c
│   ├── minipcm
│   ├── parallel-hashmap
│   └── rdma-core
└── utils
~~~

### ThirdParty

| project             | Version  |
| ------------------- | -------  |
| HdrHistogram_c      | latest   |
| parallel-hashmap    | latest   |
| atomic_queue        | latest   |
| rdma-core           | special  |
| minipcm             | special  |




### Getting help

Working in the process...


### Contact

email at chenxuz@zju.edu.cn


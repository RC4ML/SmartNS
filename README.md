# SmartNS: Enabling Line-rate and Flexible Network Stack with SmartNIC [EuroSys26]

A SmartNIC-centric network stack with software transport programmability and 400Gbps line-rate packet processing capabilities.


## Required hardware and software

- 2 x BlueField-3 SmartNICs with a 400Gbps Ethernet link
- Required libraries: cmake, gflags, numa, pthread
- Huge pages: at least 2048 huge pages on each NUMA node
- g++ >= 11.3.0
- MLNX_OFED
- Intel Sapphire Rapids CPU or Emerald Rapids CPU with Data Streaming Accelerator (DSA) support (recommended)


## Install Dependencies and Build
See [INSTALL.md](./doc/INSTALL.md) for dependency installation and build instructions on a BlueField-3-equipped machine.

## Connect and Deploy SmartNS
See [DEPLOY.md](./doc/DEPLOY.md) for host configuration, build, and deployment on BlueField-3.
For AE-only temporary machine access (jump host/key details), see [APPENDIX.md](./doc/APPENDIX.md).

## Run Test
Run the verification steps in [EXP.md](./doc/EXP.md). If the checks in the "Evaluation" document pass, the environment is correctly configured.

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
├── kernel (SmartNS kernel module code)
├── lib (SmartNS library for upper-layer applications)
├── scripts (BF3 monitoring Python scripts)
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

## Third-Party Dependencies

| project             | Version  |
| ------------------- | -------  |
| HdrHistogram_c      | latest   |
| parallel-hashmap    | latest   |
| atomic_queue        | latest   |
| rdma-core           | special  |
| minipcm             | special  |




## Getting help

Documentation is being continuously improved.


## Contact

Email: chenxuz@zju.edu.cn

## Cite
~~~
Working in the process...
~~~
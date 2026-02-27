#!/bin/bash
set -xe

# run this script on relay
THREADS_MAX=11
PAYLOAD_SIZE=4096

for i in $(seq 10 $THREADS_MAX); do
    # start server
    ssh arm-down "./tx_rdma_assisted -deviceName mlx5_2 -batch_size 1 -threads $i -outstanding 128 -payload_size $PAYLOAD_SIZE -is_server" &
    sleep 5
    # start relay
    build_dpu/tx_rdma_assisted -deviceName mlx5_2 -batch_size 1 -threads $i -outstanding 128 -payload_size $PAYLOAD_SIZE -is_server -serverIp 10.0.0.201 | grep Gpbs >>test/tx_rdma_assisted.log &
    PID=$!
    sleep 5
    # start client
    ssh pcie-up "./SmartNS/build_host/tx_rdma_assisted -deviceName mlx5_0 -batch_size 1 -threads $i -outstanding 128 -payload_size $PAYLOAD_SIZE -serverIp 10.0.0.101" &
    # wait for relay to finish
    wait $PID
    # kill server and client
    ssh arm-down "pkill tx_rdma_assisted"
    ssh pcie-up "pkill tx_rdma_assisted"
    sleep 5
done

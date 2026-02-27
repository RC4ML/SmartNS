#!/bin/bash
set -xe

# run this script on relay
THREADS_MAX=12
PAYLOAD_SIZE=2048

for i in $(seq 1 $THREADS_MAX); do
    # start server
    ssh arm-down "./tx_dma_assisted -deviceName mlx5_2 -batch_size 1 -threads $i -outstanding 128 -payload_size $PAYLOAD_SIZE -is_server" &
    sleep 5
    # start relay
    build_dpu/tx_dma_assisted -deviceName mlx5_2 -batch_size 1 -threads $i -outstanding 128 -payload_size $PAYLOAD_SIZE -is_server -serverIp 10.0.0.201 | grep Gpbs >>test/tx_dma_assisted.log &
    PID=$!
    sleep 5
    # start client
    ssh pcie-up "./SmartNS/build_host/tx_dma_assisted -deviceName mlx5_0 -batch_size 1 -threads $i -outstanding 128 -payload_size $PAYLOAD_SIZE -serverIp 10.0.0.101" &
    # wait for relay to finish
    wait $PID
    # kill server and client
    ssh arm-down "pkill tx_dma_assisted"
    ssh pcie-up "pkill tx_dma_assisted"
    sleep 5
done

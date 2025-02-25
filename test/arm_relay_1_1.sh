#!/bin/bash
set -xe

# run this script on relay
THREADS_MAX=11
PAYLOAD_SIZE=4096

for i in $(seq 10 $THREADS_MAX); do
    # start server
    ssh arm-down "./arm_relay_1_1 -deviceName mlx5_2 -batch_size 1 -threads $i -outstanding 128 -payload_size $PAYLOAD_SIZE -is_server" &
    sleep 5
    # start relay
    build_dpu/arm_relay_1_1 -deviceName mlx5_2 -batch_size 1 -threads $i -outstanding 128 -payload_size $PAYLOAD_SIZE -is_server -serverIp 10.0.0.201 | grep Gpbs >>test/arm_relay_1_1.log &
    PID=$!
    sleep 5
    # start client
    ssh pcie-up "./SmartNS/build_host/arm_relay_1_1 -deviceName mlx5_0 -batch_size 1 -threads $i -outstanding 128 -payload_size $PAYLOAD_SIZE -serverIp 10.0.0.101" &
    # wait for relay to finish
    wait $PID
    # kill server and client
    ssh arm-down "pkill arm_relay_1_1"
    ssh pcie-up "pkill arm_relay_1_1"
    sleep 5
done

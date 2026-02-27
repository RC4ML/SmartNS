project(SMARTNSTEST)

add_executable(test_context ${PROJECT_SOURCE_DIR}/test_context.cpp)

add_executable(send_bw ${PROJECT_SOURCE_DIR}/send_bw.cpp)

add_executable(write_bw ${PROJECT_SOURCE_DIR}/write_bw.cpp)

add_executable(write_bw_libr ${PROJECT_SOURCE_DIR}/write_bw_libr.cpp)
add_executable(relay_ws_stress ${PROJECT_SOURCE_DIR}/relay_ws_stress.cpp)

add_executable(tx_rdma_assisted ${PROJECT_SOURCE_DIR}/tx_rdma_assisted.cpp)
add_executable(tx_dma_assisted ${PROJECT_SOURCE_DIR}/tx_dma_assisted.cpp)
add_executable(tx_header_only_offloading ${PROJECT_SOURCE_DIR}/tx_header_only_offloading.cpp)
add_executable(rx_rdma_assisted ${PROJECT_SOURCE_DIR}/rx_rdma_assisted.cpp)
add_executable(rx_dma_assisted ${PROJECT_SOURCE_DIR}/rx_dma_assisted.cpp)
add_executable(rx_unlimited_working_set_in_cache ${PROJECT_SOURCE_DIR}/rx_unlimited_working_set_in_cache.cpp)


add_executable(test_pipe ${PROJECT_SOURCE_DIR}/test_pipe.cpp)
add_executable(linked_list_rdma ${PROJECT_SOURCE_DIR}/linked_list_rdma.cpp)
add_executable(linked_list_relay ${PROJECT_SOURCE_DIR}/linked_list_relay.cpp)

add_executable(snap_bench ${PROJECT_SOURCE_DIR}/snap_bench.cpp)
add_executable(solar_bench ${PROJECT_SOURCE_DIR}/solar_bench.cpp)

target_link_libraries(test_context smartns)

target_link_libraries(send_bw smartns)

target_link_libraries(write_bw smartns)

target_link_libraries(write_bw_libr smartns)
target_link_libraries(relay_ws_stress pthread)

target_link_libraries(tx_rdma_assisted smartns)
target_link_libraries(tx_dma_assisted smartns)
target_link_libraries(tx_header_only_offloading smartns)
target_link_libraries(rx_rdma_assisted smartns)
target_link_libraries(rx_dma_assisted smartns)
target_link_libraries(rx_unlimited_working_set_in_cache smartns)
target_link_libraries(linked_list_rdma smartns)
target_link_libraries(linked_list_relay smartns)
target_link_libraries(snap_bench smartns)
target_link_libraries(solar_bench smartns)

target_link_libraries(test_pipe smartns)

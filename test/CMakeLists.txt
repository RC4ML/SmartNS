project(SMARTNSTEST)

add_executable(test_context ${PROJECT_SOURCE_DIR}/test_context.cpp)

add_executable(send_bw ${PROJECT_SOURCE_DIR}/send_bw.cpp)

add_executable(write_bw ${PROJECT_SOURCE_DIR}/write_bw.cpp)

add_executable(write_bw_libr ${PROJECT_SOURCE_DIR}/write_bw_libr.cpp)

add_executable(arm_relay_1_1 ${PROJECT_SOURCE_DIR}/arm_relay_1_1.cpp)
add_executable(arm_relay_1_2 ${PROJECT_SOURCE_DIR}/arm_relay_1_2.cpp)
add_executable(arm_relay_1_3 ${PROJECT_SOURCE_DIR}/arm_relay_1_3.cpp)
add_executable(arm_relay_2_1 ${PROJECT_SOURCE_DIR}/arm_relay_2_1.cpp)
add_executable(arm_relay_2_2 ${PROJECT_SOURCE_DIR}/arm_relay_2_2.cpp)
add_executable(arm_relay_2_3 ${PROJECT_SOURCE_DIR}/arm_relay_2_3.cpp)


add_executable(test_pipe ${PROJECT_SOURCE_DIR}/test_pipe.cpp)
add_executable(linked_list_rdma ${PROJECT_SOURCE_DIR}/linked_list_rdma.cpp)
add_executable(linked_list_relay ${PROJECT_SOURCE_DIR}/linked_list_relay.cpp)

add_executable(snap_bench ${PROJECT_SOURCE_DIR}/snap_bench.cpp)
add_executable(solar_bench ${PROJECT_SOURCE_DIR}/solar_bench.cpp)

target_link_libraries(test_context smartns)

target_link_libraries(send_bw smartns)

target_link_libraries(write_bw smartns)

target_link_libraries(write_bw_libr smartns)

target_link_libraries(arm_relay_1_1 smartns)
target_link_libraries(arm_relay_1_2 smartns)
target_link_libraries(arm_relay_1_3 smartns)
target_link_libraries(arm_relay_2_1 smartns)
target_link_libraries(arm_relay_2_2 smartns)
target_link_libraries(arm_relay_2_3 smartns)
target_link_libraries(linked_list_rdma smartns)
target_link_libraries(linked_list_relay smartns)
target_link_libraries(snap_bench smartns)
target_link_libraries(solar_bench smartns)

target_link_libraries(test_pipe smartns)
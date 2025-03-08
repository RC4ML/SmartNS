project(SMARTNSTEST)

add_executable(test_context ${PROJECT_SOURCE_DIR}/test_context.cpp)

add_executable(send_bw ${PROJECT_SOURCE_DIR}/send_bw.cpp)

add_executable(write_bw ${PROJECT_SOURCE_DIR}/write_bw.cpp)

add_executable(write_bw_libr ${PROJECT_SOURCE_DIR}/write_bw_libr.cpp)

add_executable(arm_relay_1_1 ${PROJECT_SOURCE_DIR}/arm_relay_1_1.cpp)
add_executable(arm_relay_1_2 ${PROJECT_SOURCE_DIR}/arm_relay_1_2.cpp)

add_executable(test_pipe ${PROJECT_SOURCE_DIR}/test_pipe.cpp)

target_link_libraries(test_context smartns)

target_link_libraries(send_bw smartns)

target_link_libraries(write_bw smartns)

target_link_libraries(write_bw_libr smartns)

target_link_libraries(arm_relay_1_1 smartns)
target_link_libraries(arm_relay_1_2 smartns)

target_link_libraries(test_pipe smartns)
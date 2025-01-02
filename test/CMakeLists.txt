project(SMARTNSTEST)

add_executable(test_context ${PROJECT_SOURCE_DIR}/test_context.cpp)

add_executable(send_bw ${PROJECT_SOURCE_DIR}/send_bw.cpp)

add_executable(write_bw ${PROJECT_SOURCE_DIR}/write_bw.cpp)

target_link_libraries(test_context smartns)

target_link_libraries(send_bw smartns)

target_link_libraries(write_bw smartns)

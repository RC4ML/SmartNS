project(SMARTNSTEST)

add_executable(test_context ${PROJECT_SOURCE_DIR}/test_context.cpp)

target_link_libraries(test_context smartns)

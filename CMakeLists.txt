cmake_minimum_required(VERSION 3.0)

execute_process(COMMAND cat /opt/mellanox/doca/applications/VERSION OUTPUT_VARIABLE DOCA_VERSION)
string(STRIP ${DOCA_VERSION} DOCA_VERSION)

message(STATUS "DOCA_VERSION: ${DOCA_VERSION}")

project(SmartNS VERSION ${DOCA_VERSION})

set(SMARTNS_LOG_LEVEL "info" CACHE  STRING "Logging level (none/error/warn/info/reorder/trace/cc)") 
# Logging level
if(SMARTNS_LOG_LEVEL STREQUAL "none")
  message(STATUS "SMARTNS Logging level = none.")
  add_definitions(-DSMARTNS_LOG_LEVEL=0)
elseif(SMARTNS_LOG_LEVEL STREQUAL "error")
  message(STATUS "SMARTNS Logging level = error.")
  add_definitions(-DSMARTNS_LOG_LEVEL=1)
elseif(SMARTNS_LOG_LEVEL STREQUAL "warn")
  message(STATUS "SMARTNS Logging level = warn.")
  add_definitions(-DSMARTNS_LOG_LEVEL=2)
elseif(SMARTNS_LOG_LEVEL STREQUAL "info")
  message(STATUS "SMARTNS Logging level = info.")
  add_definitions(-DSMARTNS_LOG_LEVEL=3)
elseif(SMARTNS_LOG_LEVEL STREQUAL "reorder")
  message(STATUS "SMARTNS Logging level = reorder. Warning: Performance will be low.")
  add_definitions(-DSMARTNS_LOG_LEVEL=4)
elseif(SMARTNS_LOG_LEVEL STREQUAL "trace")
  message(STATUS "SMARTNS Logging level = trace. Trace, on! Warning: Performance will be low.")
  add_definitions(-DSMARTNS_LOG_LEVEL=5)
elseif(SMARTNS_LOG_LEVEL STREQUAL "cc")
  message(STATUS "SMARTNS Logging level = cc. Warning: Performance will be low.")
  add_definitions(-DSMARTNS_LOG_LEVEL=6)
else()
  message(STATUS "No logging level specified. Using warning level.")
  add_definitions(-DSMARTNS_LOG_LEVEL=2)
endif()


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    message(STATUS "Compile on DPU")
    message(STATUS "Output Dir is /build_dpu")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build_dpu)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build_dpu)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build_dpu)

elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    message(STATUS "Compile on HOST")
    message(STATUS "Output Dir is /build_host")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build_host)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build_host)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build_host)
else()
    # 在其他架构下的逻辑代码
    message("Unknown architecture detected")
    return()
endif ()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -std=c++20 -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-function")

add_definitions(-DDOCA_ALLOW_EXPERIMENTAL_API)


#include 
include_directories(${CMAKE_SOURCE_DIR}/include)

#include: utils
include_directories(${CMAKE_SOURCE_DIR}/utils)

#include 
include_directories(${CMAKE_SOURCE_DIR}/lib)

#sub-project: HdrHistogram
include_directories(${CMAKE_SOURCE_DIR}/third_party/HdrHistogram_c/include)

# Common sub-projects: atomic_queue
include_directories(${CMAKE_SOURCE_DIR}/third_party/atomic_queue/include)

# Common sub-projects: parallel_hashmap
include_directories(${CMAKE_SOURCE_DIR}/third_party/parallel-hashmap/parallel_hashmap)


option(HDR_HISTOGRAM_BUILD_PROGRAMS OFF)
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/HdrHistogram_c)

add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/minipcm)


set(UTILSOURCES
    ${CMAKE_SOURCE_DIR}/utils/gflags_common.cpp
    ${CMAKE_SOURCE_DIR}/utils/hdr_histogram.cpp
    ${CMAKE_SOURCE_DIR}/utils/numautil.cpp
    ${CMAKE_SOURCE_DIR}/utils/buddy.cpp
    ${CMAKE_SOURCE_DIR}/utils/allocator.cpp
    ${CMAKE_SOURCE_DIR}/utils/Crc32.cpp
)

set(DEVXSOURCES 
    ${CMAKE_SOURCE_DIR}/src/devx/devx_device.cpp
    ${CMAKE_SOURCE_DIR}/src/devx/devx_mr.cpp
    ${CMAKE_SOURCE_DIR}/src/tcp_cm/tcp_cm.cpp
    ${CMAKE_SOURCE_DIR}/src/rdma_cm/libr.cpp
    ${CMAKE_SOURCE_DIR}/src/dma/dma.cpp
    ${CMAKE_SOURCE_DIR}/src/raw_packet/raw_packet.cpp
)

set(RXESOURCES
    ${CMAKE_SOURCE_DIR}/src/rxe/rxe_recv.cpp
    ${CMAKE_SOURCE_DIR}/src/rxe/rxe_req.cpp
    ${CMAKE_SOURCE_DIR}/src/rxe/rxe_opcode.c
) 

set(DPUSOURCES
    ${CMAKE_SOURCE_DIR}/src/dpu/main.cpp
    ${CMAKE_SOURCE_DIR}/src/dpu/datapath.cpp
    ${CMAKE_SOURCE_DIR}/src/dpu/controlpath.cpp
    ${CMAKE_SOURCE_DIR}/src/dpu/config.cpp
)

set(EXTRASOURCES 
    ${CMAKE_SOURCE_DIR}/src/rdma_cm/libsmartns.cpp
)

set(LIBRARIES ${LIBRARIES} mlx5 ibverbs pthread numa gflags hdr_histogram_static minipcm_static)

add_executable(smartns_dpu ${UTILSOURCES} ${DEVXSOURCES} ${RXESOURCES} ${DPUSOURCES})
target_link_libraries(smartns_dpu ${LIBRARIES})

add_subdirectory(${CMAKE_SOURCE_DIR}/lib)
add_subdirectory(${CMAKE_SOURCE_DIR}/test)


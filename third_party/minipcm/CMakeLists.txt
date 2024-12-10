PROJECT(MINIPCM)

target_include_directories("${PROJECT_SOURCE_DIR}/include")

file(GLOB PCMRSOURCES ${PROJECT_SOURCE_DIR}/src/*.cpp)
list(APPEND PCMSOURCES ${PCMRSOURCES})

add_library(minipcm_static STATIC ${PCMSOURCES})

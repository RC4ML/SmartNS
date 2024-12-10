PROJECT(MINIPCM)


file(GLOB PCMRSOURCES ${PROJECT_SOURCE_DIR}/src/*.cpp)
list(APPEND PCMSOURCES ${PCMRSOURCES})

add_library(minipcm_static STATIC ${PCMSOURCES})
target_include_directories(minipcm_static PUBLIC ${PROJECT_SOURCE_DIR}/include)

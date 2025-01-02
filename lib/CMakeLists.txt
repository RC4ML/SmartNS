project(LIBSMARTNS)

add_library(smartns ${PROJECT_SOURCE_DIR}/smartns_dv.cpp ${UTILSOURCES} ${DEVXSOURCES} ${EXTRASOURCES})

target_link_libraries(smartns ${LIBRARIES})
cmake_minimum_required(VERSION 3.0.0)
project(libQFC VERSION 0.1.0 LANGUAGES C)

include(CTest)
enable_testing()

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

find_library(AVCODEC_LIB avcodec)
find_library(AVFORMAT_LIB avformat)
find_library(AVUTIL_LIB avutil)
find_library(SWSCALE_LIB swscale)

add_compile_definitions(BUILDING_DLL)

add_library(libQFC SHARED QFC.c QFC.h)
#add_executable(libQFC QFC.c QFC.h)

target_link_libraries(libQFC Threads::Threads ${AVCODEC_LIB} ${AVFORMAT_LIB} ${AVUTIL_LIB} ${SWSCALE_LIB})

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

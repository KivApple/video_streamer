cmake_minimum_required(VERSION 3.10)
project(video_streamer)

set(CMAKE_CXX_STANDARD 14)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake;${CMAKE_MODULE_PATH}")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(JPEG REQUIRED)

find_package(EasyLoggingPP REQUIRED)

include_directories(${JPEG_INCLUDE_DIR})

add_definitions(-DELPP_FEATURE_CRASH_LOG -DELPP_THREAD_SAFE)

add_library(video_streamer_lib SHARED src/video_streamer.cpp src/jpeg_frame.cpp src/v4l2_device.cpp)
target_link_libraries(video_streamer_lib Threads::Threads EasyLoggingPP::EasyLoggingPP ${JPEG_LIBRARIES})

add_executable(video_streamer src/video_streamer_main.cpp)
target_link_libraries(video_streamer video_streamer_lib EasyLoggingPP::EasyLoggingPP)

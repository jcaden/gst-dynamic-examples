cmake_minimum_required(VERSION 3.5)
project(dynamic_examples)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -DHAVE_CONFIG_H -Werror -Wall -Werror=declaration-after-statement -Wno-deprecated-declarations -O2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -DHAVE_CONFIG_H -Wall -Werror -std=c++11 -O2")

find_package(PkgConfig)

pkg_check_modules(GSTREAMER gstreamer-1.0 REQUIRED)
pkg_check_modules(GSTREAMER_VIDEO gstreamer-video-1.0 REQUIRED)
pkg_check_modules(GTK3 gtk+-3.0 REQUIRED)

add_executable(change_filter change_filter.c)
target_include_directories(change_filter PRIVATE ${GTK3_INCLUDE_DIRS} ${GSTREAMER_INCLUDE_DIRS} ${GSTREAMER_VIDEO_INCLUDE_DIRS})
target_link_libraries(change_filter ${GTK3_LIBRARIES} ${GSTREAMER_LIBRARIES} ${GSTREAMER_VIDEO_LIBRARIES})

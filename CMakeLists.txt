cmake_minimum_required(VERSION 3.5)
project(dumptorrent)

set(SOURCE_DUMP dumptorrent.c benc.c benc.h scrapec.c scrapec.h sha1.c sha1.h common.h)
set(SOURCE_SCRAPE scrapec.c scrapec.h benc.c benc.h sha1.c sha1.h common.h)

add_executable(dumptorrent ${SOURCE_DUMP})
add_executable(scrapec ${SOURCE_SCRAPE})

target_compile_definitions(scrapec PUBLIC BUILD_MAIN)
if(WIN32)
  target_link_libraries(dumptorrent wsock32 ws2_32)
  target_link_libraries(scrapec wsock32 ws2_32)
  target_compile_definitions(dumptorrent PUBLIC _CRT_SECURE_NO_WARNINGS)
  target_compile_definitions(scrapec PUBLIC _CRT_SECURE_NO_WARNINGS BUILD_MAIN)
endif()

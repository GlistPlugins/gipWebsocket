cmake_minimum_required(VERSION 3.8)
project(gipWebsocket)

file(GLOB_RECURSE SOURCE_FILES 
    "${CMAKE_CURRENT_LIST_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/*.h"
)

add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME} PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/src"
)

include_directories("${CMAKE_CURRENT_LIST_DIR}/src")

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

if(WIN32)
    # Windows
    target_link_libraries(${PROJECT_NAME} PUBLIC ws2_32)
elseif(ANDROID)
    # Android NDK
    target_link_libraries(${PROJECT_NAME} PUBLIC log)
elseif(IOS OR APPLE)
    # iOS & macOS (Apple Güvenli Að Çatýsý)
    target_link_libraries(${PROJECT_NAME} PUBLIC "-framework Security" "-framework CoreFoundation")
elseif(EMSCRIPTEN)
    # Web / HTML5
    target_link_libraries(${PROJECT_NAME} PUBLIC websocket.js)
elseif(UNIX AND NOT APPLE)
    # Linux
    target_link_libraries(${PROJECT_NAME} PUBLIC pthread)
endif()

include_directories("${CMAKE_CURRENT_LIST_DIR}/src")
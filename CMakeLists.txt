cmake_minimum_required(VERSION 3.20)

project(UsbLinkStabilityTester VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(usblink
    src/common/Dummy.cpp
)

target_include_directories(usblink
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(usblink_app
    app/main.cpp
)

target_link_libraries(usblink_app
    PRIVATE usblink
)

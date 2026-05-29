cmake_minimum_required(VERSION 3.20)

project(UsbLinkStabilityTester VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(usblink
    src/common/Dummy.cpp
    src/protocol/PacketEncoder.cpp
    src/protocol/PayloadGenerator.cpp
    src/protocol/SequenceTracker.cpp
    src/runtime/RuntimeSession.cpp
    src/core/RingBuffer.cpp
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

include(CTest)
if(BUILD_TESTING)
    set(USBLINK_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
    set(USBLINK_CATCH2_DIR "${USBLINK_THIRD_PARTY_DIR}/Catch2")

    if(EXISTS "${USBLINK_CATCH2_DIR}/CMakeLists.txt")
        set(CATCH_DEVELOPMENT_BUILD OFF CACHE BOOL "" FORCE)
        set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(CATCH_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(CATCH_BUILD_EXTRA_TESTS OFF CACHE BOOL "" FORCE)
        set(CATCH_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
        set(CATCH_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
        set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
        set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)

        add_subdirectory(
            "${USBLINK_CATCH2_DIR}"
            "${CMAKE_BINARY_DIR}/third_party/Catch2"
            EXCLUDE_FROM_ALL
        )
    else()
        find_package(Catch2 3 REQUIRED)
    endif()

    add_executable(usblink_tests
        tests/protocol/PacketParserTests.cpp
        tests/protocol/PayloadGeneratorTests.cpp
        tests/protocol/SequenceTrackerTests.cpp
        tests/runtime/RuntimeSessionTests.cpp
    )

    target_link_libraries(usblink_tests
        PRIVATE
            usblink
            Catch2::Catch2WithMain
    )

    include(Catch)
    catch_discover_tests(usblink_tests)
endif()

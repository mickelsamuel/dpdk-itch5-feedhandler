cmake_minimum_required(VERSION 3.16)
project(dpdk-itch5-feedhandler
    VERSION 1.0.0
    DESCRIPTION "DPDK-based NASDAQ ITCH 5.0 Feed Handler"
    LANGUAGES CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -mtune=native")

# HFT-specific optimizations for Release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto -ffast-math")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -funroll-loops")

    # Prefer static linking for performance
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto")
endif()

# Options
option(USE_DPDK "Build with DPDK support" OFF)
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" ON)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)

# Find DPDK if enabled
if(USE_DPDK)
    # DPDK uses pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DPDK REQUIRED libdpdk)

    add_definitions(-DUSE_DPDK)
    include_directories(${DPDK_INCLUDE_DIRS})
    link_directories(${DPDK_LIBRARY_DIRS})
endif()

# Find threads
find_package(Threads REQUIRED)

# Main library (header-only for now, but can be expanded)
add_library(itch5_feedhandler INTERFACE)
target_include_directories(itch5_feedhandler INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Main executable
add_executable(feed_handler
    src/main.cpp
)

target_link_libraries(feed_handler PRIVATE
    itch5_feedhandler
    Threads::Threads
)

if(USE_DPDK)
    target_link_libraries(feed_handler PRIVATE ${DPDK_LIBRARIES})
    target_compile_options(feed_handler PRIVATE ${DPDK_CFLAGS})
endif()

# Tests
if(BUILD_TESTS)
    enable_testing()

    # Test: Ring Buffer
    add_executable(test_ring_buffer tests/test_ring_buffer.cpp)
    target_link_libraries(test_ring_buffer PRIVATE
        itch5_feedhandler
        Threads::Threads
    )
    add_test(NAME RingBufferTest COMMAND test_ring_buffer)

    # Test: ITCH Parser
    add_executable(test_parser tests/test_parser.cpp)
    target_link_libraries(test_parser PRIVATE
        itch5_feedhandler
        Threads::Threads
    )
    add_test(NAME ParserTest COMMAND test_parser)

    # Test: MoldUDP64
    add_executable(test_moldudp64 tests/test_moldudp64.cpp)
    target_link_libraries(test_moldudp64 PRIVATE
        itch5_feedhandler
        Threads::Threads
    )
    add_test(NAME MoldUDP64Test COMMAND test_moldudp64)
endif()

# Benchmarks
if(BUILD_BENCHMARKS)
    # Benchmark: Ring Buffer throughput
    add_executable(bench_ring_buffer tests/bench_ring_buffer.cpp)
    target_link_libraries(bench_ring_buffer PRIVATE
        itch5_feedhandler
        Threads::Threads
    )

    # Benchmark: Parser throughput
    add_executable(bench_parser tests/bench_parser.cpp)
    target_link_libraries(bench_parser PRIVATE
        itch5_feedhandler
        Threads::Threads
    )
endif()

# Installation
install(TARGETS feed_handler
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)

install(PROGRAMS
    scripts/setup_dpdk_env.sh
    scripts/itch_to_pcap.py
    DESTINATION bin
)

# CPack for packaging
set(CPACK_PACKAGE_NAME "dpdk-itch5-feedhandler")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
include(CPack)

# Print configuration
message(STATUS "")
message(STATUS "Configuration:")
message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  DPDK support:   ${USE_DPDK}")
message(STATUS "  Build tests:    ${BUILD_TESTS}")
message(STATUS "  Build benchmarks: ${BUILD_BENCHMARKS}")
message(STATUS "  C++ compiler:   ${CMAKE_CXX_COMPILER}")
message(STATUS "  C++ flags:      ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}}")
message(STATUS "")

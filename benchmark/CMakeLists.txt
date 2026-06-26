cmake_minimum_required(VERSION 3.16)
project(hwbench C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
    -O2
)

# Link math library
find_library(M_LIB m)

# Source files
set(SOURCES
    src/main.c
    src/system_info.c
    src/cpu.c
    src/memory.c
    src/storage.c
    src/compression.c
    src/hashing.c
    src/floating_point.c
    src/thread_bench.c
    src/fs_metadata.c
    src/python_runner.c
    src/json_writer.c
    src/csv_writer.c
)

add_executable(hwbench ${SOURCES})

target_include_directories(hwbench PRIVATE include)

# Link pthreads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(hwbench PRIVATE Threads::Threads)

# Link math
if(M_LIB)
    target_link_libraries(hwbench PRIVATE ${M_LIB})
endif()

# macOS: no additional flags needed (POSIX is default)
if(APPLE)
    # Suppress deprecation warnings for POSIX functions on macOS
    target_compile_definitions(hwbench PRIVATE _DARWIN_C_SOURCE)
endif()

# Install target (optional)
install(TARGETS hwbench DESTINATION bin)

message(STATUS "hwbench build configuration:")
message(STATUS "  C compiler   : ${CMAKE_C_COMPILER}")
message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  System       : ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")

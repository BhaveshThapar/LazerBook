cmake_minimum_required(VERSION 3.24)
project(lazerbook VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(LAZERBOOK_BUILD_TESTS     "Build unit tests"                 ON)
option(LAZERBOOK_BUILD_BENCH     "Build benchmarks"                 OFF)
option(LAZERBOOK_BUILD_FUZZ      "Build libFuzzer harness (clang)"  OFF)
option(LAZERBOOK_WITH_LIQUIBOOK  "Differential test vs liquibook"   OFF)
option(LAZERBOOK_ENABLE_ASAN     "AddressSanitizer"                 OFF)
option(LAZERBOOK_ENABLE_UBSAN    "UndefinedBehaviorSanitizer"       OFF)
option(LAZERBOOK_ENABLE_TSAN     "ThreadSanitizer"                  OFF)
option(LAZERBOOK_COVERAGE        "Coverage instrumentation"         OFF)

# --- Warning flags (INTERFACE) ---------------------------------------------
add_library(lazerbook_warnings INTERFACE)
target_compile_options(lazerbook_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast
    -Wcast-align -Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion
    -Wnull-dereference -Wdouble-promotion -Wformat=2 -Werror)

# --- Sanitizers (INTERFACE) ------------------------------------------------
add_library(lazerbook_sanitizers INTERFACE)
set(_lzb_san "")
if(LAZERBOOK_ENABLE_ASAN)
    list(APPEND _lzb_san address)
endif()
if(LAZERBOOK_ENABLE_UBSAN)
    list(APPEND _lzb_san undefined)
endif()
if(LAZERBOOK_ENABLE_TSAN)
    list(APPEND _lzb_san thread)
endif()
if(_lzb_san)
    list(JOIN _lzb_san "," _lzb_san_joined)
    target_compile_options(lazerbook_sanitizers INTERFACE
        -fsanitize=${_lzb_san_joined} -fno-omit-frame-pointer -g)
    target_link_options(lazerbook_sanitizers INTERFACE
        -fsanitize=${_lzb_san_joined})
endif()

# --- Coverage (INTERFACE) --------------------------------------------------
add_library(lazerbook_coverage INTERFACE)
if(LAZERBOOK_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(lazerbook_coverage INTERFACE
            -fprofile-instr-generate -fcoverage-mapping)
        target_link_options(lazerbook_coverage INTERFACE
            -fprofile-instr-generate -fcoverage-mapping)
    else()
        target_compile_options(lazerbook_coverage INTERFACE --coverage)
        target_link_options(lazerbook_coverage INTERFACE --coverage)
    endif()
endif()

# --- Core static library ---------------------------------------------------
add_library(lazerbook STATIC
    src/itch.cpp src/book.cpp src/matcher.cpp src/reconstruct.cpp
    src/fba.cpp src/sim.cpp src/itch_synth.cpp src/framing.cpp)
target_include_directories(lazerbook PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(lazerbook PRIVATE
    lazerbook_warnings lazerbook_sanitizers lazerbook_coverage)

if(LAZERBOOK_WITH_LIQUIBOOK)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/liquibook/src/book/order_book.h")
        message(FATAL_ERROR
            "LAZERBOOK_WITH_LIQUIBOOK=ON but third_party/liquibook is missing. "
            "Run: git submodule update --init --recursive")
    endif()
    add_library(liquibook_iface INTERFACE)
    target_include_directories(liquibook_iface SYSTEM INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/liquibook/src")
    target_compile_definitions(lazerbook PUBLIC LAZERBOOK_WITH_LIQUIBOOK=1)
endif()

if(LAZERBOOK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(LAZERBOOK_BUILD_BENCH)
    add_subdirectory(bench)
endif()

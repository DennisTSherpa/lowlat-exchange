cmake_minimum_required(VERSION 3.20)
project(exchange LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(EXCHANGE_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(EXCHANGE_PYTHON "Build the Python module (exchange_py)" ON)
if(EXCHANGE_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# --- The order book library --------------------------------------------------
# Position-independent code so the same library can also go into the Python module.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_library(exchange
  cpp/src/order_book.cpp
  cpp/src/event_log.cpp
  cpp/src/matching_engine.cpp
  cpp/src/tcp.cpp)
target_include_directories(exchange PUBLIC cpp/include)

# --- Third-party test and benchmark libraries (downloaded at configure time) --
include(FetchContent)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(benchmark
  URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest benchmark)

# --- Tests -------------------------------------------------------------------
enable_testing()
add_executable(exchange_tests
  cpp/tests/order_book_test.cpp
  cpp/tests/random_compare_test.cpp
  cpp/tests/engine_test.cpp
  cpp/tests/tcp_test.cpp)
find_package(Threads REQUIRED)
target_link_libraries(exchange_tests PRIVATE exchange GTest::gtest_main Threads::Threads)
include(GoogleTest)
gtest_discover_tests(exchange_tests)

# --- Benchmarks --------------------------------------------------------------
add_executable(order_book_bench cpp/bench/order_book_bench.cpp)
target_link_libraries(order_book_bench PRIVATE exchange benchmark::benchmark)

add_executable(latency cpp/bench/latency.cpp)
target_link_libraries(latency PRIVATE exchange)

# --- Command-line tools ------------------------------------------------------
add_executable(record_session cpp/tools/record_session.cpp)
target_link_libraries(record_session PRIVATE exchange)

add_executable(replay_session cpp/tools/replay_session.cpp)
target_link_libraries(replay_session PRIVATE exchange)

add_executable(exchange_server cpp/tools/exchange_server.cpp)
target_link_libraries(exchange_server PRIVATE exchange)

add_executable(load_client cpp/tools/load_client.cpp)
target_link_libraries(load_client PRIVATE exchange)

# --- Python module -----------------------------------------------------------
# Sanitizer builds skip it: a sanitized module can't be loaded by a normal Python.
if(EXCHANGE_PYTHON AND NOT EXCHANGE_SANITIZE)
  FetchContent_Declare(pybind11
    URL https://github.com/pybind/pybind11/archive/refs/tags/v2.13.6.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(pybind11)
  pybind11_add_module(exchange_py python/bindings/bindings.cpp)
  target_link_libraries(exchange_py PRIVATE exchange)
  # Put the module in build/python so `PYTHONPATH=build/python` finds it.
  set_target_properties(exchange_py PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/python)
endif()

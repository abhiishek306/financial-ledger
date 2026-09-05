cmake_minimum_required(VERSION 3.20)
project(ledger_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT UNIX)
  message(WARNING "This engine uses POSIX APIs (open/write/fsync) directly and "
                  "targets Linux. Building on non-UNIX platforms is not supported.")
endif()

option(LEDGER_ENABLE_SANITIZERS "Build with -fsanitize=address,undefined" OFF)
option(LEDGER_BUILD_TESTS "Build unit / fault-injection tests" ON)
option(LEDGER_BUILD_BENCHMARKS "Build Google Benchmark microbenchmarks (requires network fetch)" OFF)

add_library(ledger_core
  src/crc32.cpp
  src/wal_codec.cpp
  src/wal_writer.cpp
  src/wal_reader.cpp
  src/ledger_state.cpp
  src/validation.cpp
  src/idempotency_cache.cpp
  src/snapshot.cpp
  src/recovery.cpp
  src/engine.cpp
)
target_include_directories(ledger_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(ledger_core PUBLIC -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(ledger_core PUBLIC pthread)

if(LEDGER_ENABLE_SANITIZERS)
  target_compile_options(ledger_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(ledger_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(ledger_demo src/main.cpp)
target_link_libraries(ledger_demo PRIVATE ledger_core)

if(LEDGER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(LEDGER_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

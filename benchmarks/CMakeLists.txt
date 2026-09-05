cmake_minimum_required(VERSION 3.20)

include(FetchContent)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.9.0
)
FetchContent_MakeAvailable(googlebenchmark)

add_executable(bench_ledger bench_ledger.cpp)
target_link_libraries(bench_ledger PRIVATE ledger_core benchmark::benchmark)

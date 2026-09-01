include(FetchContent)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.9.4
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(benchmark)

add_executable(lobcore_bench bench_book.cpp)
target_link_libraries(lobcore_bench PRIVATE lobcore benchmark::benchmark)

add_executable(cross_sweep_profile cross_sweep_profile.cpp)
target_link_libraries(cross_sweep_profile PRIVATE lobcore)

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

# Callgrind 用。valgrind-dev が無い環境（CI 等）ではスキップする。
find_path(LOBCORE_VALGRIND_INCLUDE_DIR NAMES valgrind/callgrind.h)
if(LOBCORE_VALGRIND_INCLUDE_DIR)
  add_executable(cross_sweep_profile cross_sweep_profile.cpp)
  target_include_directories(cross_sweep_profile SYSTEM PRIVATE ${LOBCORE_VALGRIND_INCLUDE_DIR})
  target_link_libraries(cross_sweep_profile PRIVATE lobcore)
else()
  message(STATUS "Skipping cross_sweep_profile (valgrind/callgrind.h not found)")
endif()

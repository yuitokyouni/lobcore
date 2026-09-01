cmake_minimum_required(VERSION 3.21)
project(lobcore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LOBCORE_BUILD_TESTS "Build unit tests (Catch2)" ON)
option(LOBCORE_BUILD_BENCH "Build benchmarks (Google Benchmark)" OFF)
option(LOBCORE_SANITIZE "Enable ASan/UBSan in Debug builds (GCC/Clang only)" ON)

# ---- library -----------------------------------------------------------------
add_library(lobcore STATIC
  src/book.cpp
  src/log.cpp
  src/reference_book.cpp
  src/kernel/kernel.cpp
  src/kernel/rng.cpp
  src/kernel/latency_model.cpp
  src/kernel/continuous_market.cpp
  src/allocation.cpp
)
target_include_directories(lobcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
  target_compile_options(lobcore PUBLIC /W4 /permissive-)
else()
  target_compile_options(lobcore PUBLIC -Wall -Wextra -Wpedantic)
  if(LOBCORE_SANITIZE)
    target_compile_options(lobcore PUBLIC
      $<$<CONFIG:Debug>:-fsanitize=address>
      $<$<CONFIG:Debug>:-fsanitize=undefined>
      $<$<CONFIG:Debug>:-fno-omit-frame-pointer>)
    target_link_options(lobcore PUBLIC
      $<$<CONFIG:Debug>:-fsanitize=address>
      $<$<CONFIG:Debug>:-fsanitize=undefined>)
  endif()
endif()

# ---- tests / bench -----------------------------------------------------------
if(LOBCORE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(LOBCORE_BUILD_BENCH)
  add_subdirectory(bench)
endif()

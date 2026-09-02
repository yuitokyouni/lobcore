cmake_minimum_required(VERSION 3.21)

# lobcore C++ ライブラリを親から使うか、単独で取り込む。
if(NOT TARGET lobcore)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/lobcore_core)
endif()

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG QUIET)
if(NOT pybind11_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v2.13.6
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(pybind11)
endif()

pybind11_add_module(_core MODULE
  module.cpp
)
target_link_libraries(_core PRIVATE lobcore)
target_compile_features(_core PRIVATE cxx_std_20)

install(TARGETS _core DESTINATION lobcore)

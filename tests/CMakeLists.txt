include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(lobcore_tests test_book.cpp test_log.cpp test_diff.cpp test_move.cpp test_kernel.cpp test_agent.cpp test_market.cpp timestamp_listener.cpp)
target_link_libraries(lobcore_tests PRIVATE lobcore Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(lobcore_tests)

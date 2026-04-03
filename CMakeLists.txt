cmake_minimum_required(VERSION 3.24)

project(relativity_mission_control VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RELATIVITY_BUILD_VISUALIZER "Build the native raylib visualizer." ON)

add_library(
  relativity
  src/lib/simulation.cpp
)

target_include_directories(relativity PUBLIC include)

target_compile_options(
  relativity
  PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
)

add_executable(relativity_cli src/cli/main.cpp)
target_link_libraries(relativity_cli PRIVATE relativity)

enable_testing()

add_executable(relativity_tests tests/simulation_tests.cpp)
target_link_libraries(relativity_tests PRIVATE relativity)
add_test(NAME relativity_tests COMMAND relativity_tests)

if(RELATIVITY_BUILD_VISUALIZER)
  include(FetchContent)

  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
  )

  FetchContent_MakeAvailable(raylib)

  add_executable(relativity_visualizer src/visualizer/main.cpp)
  target_link_libraries(relativity_visualizer PRIVATE relativity raylib)
endif()


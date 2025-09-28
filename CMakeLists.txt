cmake_minimum_required(VERSION 3.28)

project(cpp_dyn)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CPP_DYN_BUILD_TESTS "Build tests for the library")

# Just assume Clang (for now)
if(NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
   message(FATAL_ERROR "Only Clang is supported at this time.")
endif()

# TODO: Remove these once C++26 reflection gets more implementations
add_compile_options(-stdlib=libc++ -freflection-latest)
add_link_options(-stdlib=libc++)

add_library(cpp_dyn INTERFACE)

target_include_directories(cpp_dyn INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(cpp_dyn::cpp_dyn ALIAS cpp_dyn)

if("${CPP_DYN_BUILD_TESTS}")
   # Wait for a stable compiler to link this
   # find_package(Catch2 3 REQUIRED)

   # add_executable(basic_tests "tests/basic_tests.cpp")
   # target_link_libraries(basic_tests PRIVATE cpp_dyn Catch2::Catch2WithMain)

   add_executable(compile_time_tests "tests/compile_time_tests.cpp")
   target_link_libraries(compile_time_tests PRIVATE cpp_dyn)
endif()

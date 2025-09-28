cmake_minimum_required(VERSION 3.28)

project(cpp_dyn)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CPP_DYN_BUILD_TESTS "Build tests for the library")
# Modules currently don't work, but have this for future support
# option(CPP_DYN_ENABLE_MODULE "Enable building module files")
set(CPP_DYN_ENABLE_MODULE FALSE)

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

if("${CPP_DYN_ENABLE_MODULE}")
   add_library(cpp_dyn_module)
   target_sources(
      cpp_dyn_module PUBLIC
      FILE_SET
         modules
      TYPE
         CXX_MODULES
      FILES
         src/cpp_dyn.cppm
   )
   target_link_libraries(cpp_dyn_module cpp_dyn)

   add_library(cpp_dyn::cpp_dyn_module ALIAS cpp_dyn_module)
endif()

if("${CPP_DYN_BUILD_TESTS}")
   Include(FetchContent)

   FetchContent_Declare(
      Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG        v3.10.0
   )

   FetchContent_MakeAvailable(Catch2)

   add_executable(basic_tests "tests/basic_tests.cpp")
   target_link_libraries(basic_tests PRIVATE cpp_dyn Catch2::Catch2WithMain)

   add_executable(compile_time_tests "tests/compile_time_tests.cpp")
   target_link_libraries(compile_time_tests PRIVATE cpp_dyn)

   if("${CPP_DYN_ENABLE_MODULE}")
      add_executable(module_test "tests/test_module.cpp")
      target_link_libraries(module_test PRIVATE cpp_dyn::cpp_dyn_module Catch2::Catch2WithMain)
   endif()
endif()

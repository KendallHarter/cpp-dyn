cmake_minimum_required(VERSION 3.28)

project(cpp_dyn)

option(KHCT_CPP_DYN_BUILD_TESTS "Build tests for the library" FALSE)
option(KHCT_CPP_DYN_ENABLE_MODULE "Enable building module files" FALSE)
option(KHCT_CPP_DYN_BUILD_POC "Build proof of concept demonstrations" FALSE)

add_library(cpp_dyn INTERFACE)

target_compile_features(cpp_dyn INTERFACE cxx_std_26)

# Add -freflection if GCC
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
   # There's an error with the module linking without the -fabi-version=20 flag
   # The error says something about using -fabi-version=0 but that doesn't work
   target_compile_options(cpp_dyn INTERFACE -freflection -fabi-version=20)
endif()

target_include_directories(cpp_dyn INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(khct::cpp_dyn ALIAS cpp_dyn)

if("${KHCT_CPP_DYN_ENABLE_MODULE}")
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
   target_link_libraries(cpp_dyn_module khct::cpp_dyn)

   add_library(khct::cpp_dyn::cpp_dyn_module ALIAS cpp_dyn_module)
endif()

if("${KHCT_CPP_DYN_BUILD_TESTS}")
   include(CTest)
   include(FetchContent)

   FetchContent_Declare(
      Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG        v3.10.0
   )

   FetchContent_MakeAvailable(Catch2)

   add_executable(basic_tests "tests/basic_tests.cpp")
   target_link_libraries(basic_tests PRIVATE khct::cpp_dyn Catch2::Catch2WithMain)
   add_test(NAME basic_tests COMMAND basic_tests)

   add_executable(compile_time_tests "tests/compile_time_tests.cpp")
   target_link_libraries(compile_time_tests PRIVATE khct::cpp_dyn)
   add_test(NAME compile_time_tests COMMAND compile_time_tests)

   if("${KHCT_CPP_DYN_ENABLE_MODULE}")
      add_executable(module_test "tests/test_module.cpp")
      target_link_libraries(module_test PRIVATE khct::cpp_dyn::cpp_dyn_module Catch2::Catch2WithMain)
      add_test(NAME module_test COMMAND module_test)
   endif()
endif()

if("${KHCT_CPP_DYN_BUILD_POC}")
   add_executable(better_syntax "examples/proof_of_concept/better_syntax/main.cpp")
   target_link_libraries(better_syntax PRIVATE khct::cpp_dyn)

   add_executable(better_callers "examples/proof_of_concept/better_vtable_callers/main.cpp")
   target_link_libraries(better_callers PRIVATE khct::cpp_dyn)
endif()

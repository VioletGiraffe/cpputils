# 3.20 for C++20 standard support across all three compilers
cmake_minimum_required(VERSION 3.20)

project(cpputils-tests LANGUAGES CXX)

set(TARGET_NAME tests)

# Listed in the order of SOURCES in test-app/test-app.pro, which must hold the same files.
add_executable(${TARGET_NAME}
	test-app/main.cpp
	test-app/cinterruptablethread_tests.cpp
	test-app/execution_queue_tests.cpp
	test-app/memory_functions_tests.cpp
	test-app/thread_pool_tests.cpp
)

# The library under test. Its PUBLIC include directories reach cpp-template-utils too, which is where
# the tests get catch.hpp, test_main.hpp and compiler_warnings_control.h.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/.. cpputils)
target_link_libraries(${TARGET_NAME} PRIVATE cpputils)

# The library enables it for its consumers; a test run spends the link time and gains nothing
set_target_properties(cpputils PROPERTIES INTERPROCEDURAL_OPTIMIZATION FALSE)

# CONFIG += c++latest in the .pro
target_compile_features(${TARGET_NAME} PRIVATE cxx_std_23)
# CONFIG += strict_c++ in the .pro
set_target_properties(${TARGET_NAME} PROPERTIES CXX_EXTENSIONS OFF)

# thread_pool_tests.cpp needs Catch2's benchmarking support
target_compile_definitions(${TARGET_NAME} PRIVATE CATCH_CONFIG_ENABLE_BENCHMARKING)

if (MSVC)
	target_compile_options(${TARGET_NAME} PRIVATE /permissive- /Zc:__cplusplus /MP /W4)
	target_compile_definitions(${TARGET_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
	target_compile_options(${TARGET_NAME} PRIVATE -Wall)
	target_compile_definitions(${TARGET_NAME} PRIVATE $<$<CONFIG:Debug>:_DEBUG>)
endif()

# $<0:> suppresses the per-configuration subdirectory MSBuild otherwise appends
if (DEFINED OUTPUT_DIR)
	set_target_properties(${TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}/$<0:>)
endif()

enable_testing()
add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})

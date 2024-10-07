cmake_minimum_required(VERSION 3.10)
project(cpputils LANGUAGES CXX)

file(GLOB_RECURSE SOURCES "debugger/*.cpp")
file(GLOB_RECURSE HEADERS "debugger/*.h")
file(GLOB_RECURSE SOURCES "system/*.cpp")
file(GLOB_RECURSE HEADERS "system/*.h")
file(GLOB_RECURSE SOURCES "math/*.cpp")
file(GLOB_RECURSE HEADERS "math/*.h")
file(GLOB_RECURSE SOURCES "threading/*.cpp")
file(GLOB_RECURSE HEADERS "threading/*.h")
file(GLOB_RECURSE SOURCES "assert/*.cpp")
file(GLOB_RECURSE HEADERS "assert/*.h")
file(GLOB_RECURSE SOURCES "lang/*.cpp")
file(GLOB_RECURSE HEADERS "lang/*.h")
file(GLOB_RECURSE SOURCES "hash/*.cpp")
file(GLOB_RECURSE HEADERS "hash/*.h")
file(GLOB_RECURSE SOURCES "utility_functions/*.cpp")
file(GLOB_RECURSE HEADERS "utility_functions/*.h")

# Set the library as static
add_library(cpputils STATIC ${SOURCES} ${HEADERS})

# Add include directories
target_include_directories(cpputils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/
    ${CMAKE_CURRENT_SOURCE_DIR}/../cpp-template-utils/
)

# Set output directories for release and debug
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/../bin/release")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/../bin/debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/../bin/release")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/../bin/debug")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/../bin/debug")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/../bin/debug")

# Set object directories
set(CMAKE_OBJECT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../build/")
set(CMAKE_OBJECT_OUTPUT_DIR "${CMAKE_OBJECT_PATH}/$<CONFIG>/cpputils")

# Platform-specific flags
if(MSVC)
    # Visual Studio specific flags
    target_compile_options(cpputils PRIVATE
        /MP /Zi /JMC
        /std:c++latest /permissive- /Zc:__cplusplus /Zc:char8_t
        /W4 /FS
    )
    target_link_options(cpputils PRIVATE
        "$<$<CONFIG:Release>:/OPT:REF /OPT:ICF>"
        "$<$<CONFIG:Debug>:/INCREMENTAL>"
        "/DEBUG:FASTLINK"
    )
    target_compile_definitions(cpputils PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
else()
    # Unix-like specific flags
    target_compile_options(cpputils PRIVATE
        -std=c++2b
        -pedantic-errors
        -Wall -Wextra -Wdelete-non-virtual-dtor -Werror=duplicated-cond
        -Werror=duplicated-branches -Warith-conversion -Warray-bounds
        -Wattributes -Wcast-align -Wcast-qual -Wconversion -Wdate-time
        -Wduplicated-branches -Wendif-labels -Werror=overflow
        -Werror=return-type -Werror=shift-count-overflow -Werror=sign-promo
        -Werror=undef -Wextra -Winit-self -Wlogical-op -Wmissing-include-dirs
        -Wnull-dereference -Wpedantic -Wpointer-arith -Wredundant-decls
        -Wshadow -Wstrict-aliasing -Wstrict-aliasing=3 -Wuninitialized
        -Wunused-const-variable=2 -Wwrite-strings -Wlogical-op
        -Wno-missing-include-dirs -Wno-undef
        -fconcepts
    )
    target_link_options(cpputils PRIVATE -fuse-ld=gold)

    # Add defines based on configuration
    target_compile_definitions(cpputils PRIVATE
        "$<$<CONFIG:Release>:NDEBUG=1>"
        "$<$<CONFIG:Debug>:_DEBUG>"
    )
endif()

set_target_properties(cpputils PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
    OBJECT_OUTPUTS_DIR "${CMAKE_OBJECT_OUTPUT_DIR}"
)

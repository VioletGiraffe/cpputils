cmake_minimum_required(VERSION 3.15) # Has to be 3.15+ because of this: https://cmake.org/cmake/help/latest/policy/CMP0057.html
project(cpputils LANGUAGES CXX)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

file(GLOB_RECURSE SOURCES
	"debugger/*.cpp"
	"math/*.cpp"
	"threading/*.cpp"
	"assert/*.cpp"
	"lang/*.cpp"
	"hash/*.cpp"
	"utility_functions/*.cpp"
)

file(GLOB_RECURSE HEADERS
	"debugger/*.h*"
	"system/*.h*"
	"math/*.h*"
	"threading/*.h*"
	"assert/*.h*"
	"lang/*.h*"
	"hash/*.h*"
	"utility_functions/*.h*"
)

set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "")

# Set the library as static
add_library(cpputils STATIC ${SOURCES} ${HEADERS})

target_sources(cpputils PRIVATE
		system/ctimeelapsed.cpp
		system/processfilepath.cpp
		system/consoleapplicationexithandler.cpp
		system/timing.cpp
	)

if (WIN32)
	target_sources(cpputils PRIVATE
		system/win_utils.cpp
	)
endif()

# Add include directories
target_include_directories(cpputils PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/
	${CMAKE_CURRENT_LIST_DIR}/../cpp-template-utils/
)

# Platform-specific flags
if(MSVC)
	# Visual Studio specific flags
	target_compile_options(cpputils PRIVATE
		$<$<CONFIG:Debug>:/Ob2>
		$<$<OR:$<CONFIG:RelWithDebInfo>,$<CONFIG:Release>>:/GS- /O2>
		/MP /Zi 
		/std:c++latest /permissive- /Zc:__cplusplus /Zc:char8_t
		/W4 /FS /Gy
	)
	target_link_options(cpputils PRIVATE
		$<$<OR:$<CONFIG:RelWithDebInfo>,$<CONFIG:Release>>:/OPT:REF /OPT:ICF>
		$<$<CONFIG:Debug>:/INCREMENTAL>
		/DEBUG:FASTLINK
	)
	target_compile_definitions(cpputils PRIVATE
		WIN32_LEAN_AND_MEAN
		NOMINMAX
	)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
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
		$<$<OR:$<CONFIG:RelWithDebInfo>,$<CONFIG:Release>>:-O3>
	)
	target_link_options(cpputils PRIVATE -fuse-ld=gold)

	if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
		target_compile_options(cpputils PRIVATE -fconcepts)
	endif()

	# Add defines based on configuration
	target_compile_definitions(cpputils PRIVATE
		"$<$<CONFIG:Release>:NDEBUG=1>"
		"$<$<CONFIG:Debug>:_DEBUG>"
	)
endif()

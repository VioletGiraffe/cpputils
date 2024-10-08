cmake_minimum_required(VERSION 3.15) # Has to be 3.15+ because of this: https://cmake.org/cmake/help/latest/policy/CMP0057.html
project(cpputils LANGUAGES CXX)

file(GLOB_RECURSE SOURCES
	"debugger/*.cpp"
	"system/*.cpp"
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

# Set the library as static
add_library(cpputils STATIC ${SOURCES} ${HEADERS})

# Add include directories
target_include_directories(cpputils PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/
	${CMAKE_CURRENT_LIST_DIR}/../cpp-template-utils/
)

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

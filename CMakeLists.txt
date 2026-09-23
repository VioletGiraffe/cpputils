cmake_minimum_required(VERSION 3.15) # Has to be 3.15+ because of this: https://cmake.org/cmake/help/latest/policy/CMP0057.html

set(TARGET_NAME cpputils)
project(${TARGET_NAME} LANGUAGES CXX)

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
add_library(${TARGET_NAME} STATIC ${SOURCES} ${HEADERS})

# Set output directory to the desired one, without the Release/Debug sudirs that MSBuild adds by default
if (DEFINED OUTPUT_DIR)
get_property(current_targets DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY BUILDSYSTEM_TARGETS)
	foreach(TARGET ${current_targets})
		set_target_properties(${TARGET} PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}/$<0:>
			LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR}/$<0:>
			ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR}/$<0:>
	)
endforeach()
endif()

target_sources(${TARGET_NAME} PRIVATE
		timing/profiler.cpp
		system/processfilepath.cpp
		system/consoleapplicationexithandler.cpp
		system/timing.cpp
		system/storagespeed.cpp
	)

if (WIN32)
	target_sources(${TARGET_NAME} PRIVATE
		system/win_utils.cpp
	)
endif()

if (APPLE)
	# storagespeed.cpp; PUBLIC because the static lib defers the actual linking to the consumer
	target_link_libraries(${TARGET_NAME} PUBLIC "-framework IOKit" "-framework CoreFoundation")
endif()

# Add include directories
target_include_directories(${TARGET_NAME} PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/
	${CMAKE_CURRENT_LIST_DIR}/../cpp-template-utils/
	${CMAKE_CURRENT_LIST_DIR}/cpp-template-utils/ # CI clones it inside the repo rather than beside it
)

# threading/; PUBLIC because the static lib defers the actual linking to the consumer
find_package(Threads REQUIRED)
target_link_libraries(${TARGET_NAME} PUBLIC Threads::Threads)

# Platform-specific flags
if(MSVC)
	# Visual Studio specific flags
	target_compile_options(${TARGET_NAME} PRIVATE
		$<$<OR:$<CONFIG:RelWithDebInfo>,$<CONFIG:Release>>:/GS- /O2>
		/MP /Zi 
		/std:c++latest /permissive- /Zc:__cplusplus /Zc:char8_t
		/W4 /FS /Gy
	)
	target_link_options(${TARGET_NAME} PRIVATE
		$<$<OR:$<CONFIG:RelWithDebInfo>,$<CONFIG:Release>>:/OPT:REF /OPT:ICF>
		$<$<CONFIG:Debug>:/INCREMENTAL>
		/DEBUG:FASTLINK
	)
	target_compile_definitions(${TARGET_NAME} PRIVATE
		WIN32_LEAN_AND_MEAN
		NOMINMAX
	)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
	# Unix-like specific flags
	target_compile_options(${TARGET_NAME} PRIVATE
		-std=c++2b
		-pedantic-errors
		-Wall -Wextra -Wnon-virtual-dtor -Woverloaded-virtual -Wcast-qual -Wdouble-promotion -Wfloat-conversion -Wundef
		-Wformat=2 -Wextra-semi -Wzero-as-null-pointer-constant -Wfloat-equal -Wredundant-decls -Wvla
		-Wno-missing-field-initializers
		-Werror=return-type -Werror=uninitialized -Werror=delete-non-virtual-dtor -Werror=address
		-Werror=sizeof-pointer-div -Werror=sizeof-pointer-memaccess
		$<$<OR:$<CONFIG:RelWithDebInfo>,$<CONFIG:Release>>:-O3>
	)

	if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		target_compile_options(${TARGET_NAME} PRIVATE
			-Wshadow-all -Wcast-align -Wcomma -Wconditional-uninitialized -Wheader-hygiene -Wloop-analysis -Wextra-semi-stmt -Wunreachable-code-aggressive
			-Wshorten-64-to-32 -Wmissing-prototypes -Wmissing-variable-declarations
			-Wimplicit-fallthrough -Wsuggest-override
			-Werror=return-stack-address -Werror=infinite-recursion
		)
	else()
		target_compile_options(${TARGET_NAME} PRIVATE
			-fconcepts
			-Wshadow -Wcast-align=strict -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wnull-dereference
			-Wsuggest-override -Wmissing-declarations -Wmismatched-tags -Wunused-const-variable=1
			-Werror=return-local-addr -Werror=memset-transposed-args -Werror=nonnull-compare -Werror=mismatched-new-delete -Werror=infinite-recursion
			-Wcatch-value=3 -Werror=catch-value # -Werror=catch-value on its own would only enable level 1
			-Wno-maybe-uninitialized # False positives on std::optional and std::expected
		)
	endif()

	# Add defines based on configuration
	target_compile_definitions(${TARGET_NAME} PRIVATE
		"$<$<CONFIG:Release>:NDEBUG=1>"
		"$<$<CONFIG:Debug>:_DEBUG>"
	)
endif()

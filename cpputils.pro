TEMPLATE = lib
TARGET = cpputils

CONFIG += staticlib
CONFIG -= qt
!win*:CONFIG -= flat

CONFIG += strict_c++

mac* | linux*|freebsd {
	CONFIG(release, debug|release):CONFIG *= Release optimize_full
	CONFIG(debug, debug|release):CONFIG *= Debug
}

exists(../global.pri){
	include(../global.pri)
} else {
	CONFIG += c++2b
}

android {
	Release:OUTPUT_DIR=android/release
	Debug:OUTPUT_DIR=android/debug

} else:ios {
	Release:OUTPUT_DIR=ios/release
	Debug:OUTPUT_DIR=ios/debug

} else {
	Release:OUTPUT_DIR=release
	Debug:OUTPUT_DIR=debug
}

DESTDIR  = ../bin/$${OUTPUT_DIR}/
OBJECTS_DIR = ../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../build/$${OUTPUT_DIR}/$${TARGET}

include (debugger/debugger.pri)
include (system/system.pri)
include (math/math.pri)
include (threading/threading.pri)
include (assert/assert.pri)
include (lang/lang.pri)
include (hash/hash.pri)
include (utility_functions/utility_functions.pri)
include (timing/timing.pri)

win32*:!*msvc2012:*msvc* {
	QMAKE_CXXFLAGS += /FS
}

INCLUDEPATH += \
	./ \
	../cpp-template-utils/ \
	cpp-template-utils/ #for building tests in CI workflows

win*{
	QMAKE_CXXFLAGS += /MP /Zi
	Debug:QMAKE_CXXFLAGS += /JMC
	QMAKE_CXXFLAGS += /std:c++latest /permissive- /Zc:__cplusplus /Zc:char8_t
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX
	QMAKE_CXXFLAGS_WARN_ON += /W4
	QMAKE_CXXFLAGS += /we4715 /we4716 # not all control paths return a value / must return a value
	QMAKE_CXXFLAGS += /we4172         # returning address of local variable or temporary
	QMAKE_CXXFLAGS += /we4700         # uninitialized local variable used
	QMAKE_CXXFLAGS += /we4477         # printf format string does not match the argument
	QMAKE_CXXFLAGS += /we4551         # function call missing argument list
	QMAKE_CXXFLAGS += /we4552 /we4553 # operator has no effect; did you intend '='?

	!*msvc2013*:QMAKE_LFLAGS += /DEBUG:FASTLINK

	Debug:QMAKE_LFLAGS += /INCREMENTAL
	Release:QMAKE_LFLAGS += /OPT:REF /OPT:ICF
}

linux*|mac*|freebsd{
	QMAKE_CXXFLAGS += -std=c++2b
	QMAKE_CXXFLAGS += -pedantic-errors
	QMAKE_CFLAGS += -pedantic-errors

	QMAKE_CXXFLAGS_WARN_ON += -Wall -Wextra -Wnon-virtual-dtor -Woverloaded-virtual -Wcast-qual -Wdouble-promotion
	QMAKE_CXXFLAGS_WARN_ON += -Wformat=2 -Wextra-semi -Wzero-as-null-pointer-constant -Wfloat-equal -Wredundant-decls -Wvla

	QMAKE_CXXFLAGS += -Werror=return-type -Werror=uninitialized -Werror=delete-non-virtual-dtor -Werror=address
	QMAKE_CXXFLAGS += -Werror=sizeof-pointer-div -Werror=sizeof-pointer-memaccess

	contains(QMAKE_COMPILER, clang) {
		QMAKE_CXXFLAGS_WARN_ON += -Wshadow-all -Wcast-align -Wcomma -Wconditional-uninitialized -Wheader-hygiene -Wloop-analysis -Wextra-semi-stmt -Wunreachable-code-aggressive
		QMAKE_CXXFLAGS_WARN_ON += -Wshorten-64-to-32 -Wmissing-prototypes -Wmissing-variable-declarations -Wno-weak-vtables
		QMAKE_CXXFLAGS += -Werror=return-stack-address -Werror=infinite-recursion
	} else {
		QMAKE_CXXFLAGS_WARN_ON += -Wshadow -Wcast-align=strict -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wnull-dereference
		QMAKE_CXXFLAGS_WARN_ON += -Wsuggest-override -Wnoexcept -Wmissing-declarations
		QMAKE_CXXFLAGS += -Werror=return-local-addr -Werror=memset-transposed-args -Werror=nonnull-compare -Werror=mismatched-new-delete -Werror=infinite-recursion
		QMAKE_CXXFLAGS += -Wcatch-value=3 -Werror=catch-value # -Werror=catch-value on its own would only enable level 1
	}

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}

!mac*:g++*: QMAKE_CXXFLAGS += -fconcepts

mac*{
	QMAKE_MACOSX_DEPLOYMENT_TARGET = 13.3
}

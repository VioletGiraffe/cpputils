TEMPLATE = lib
TARGET = cpputils

CONFIG += staticlib
CONFIG -= qt
CONFIG -= flat

CONFIG += strict_c++ c++_latest

mac* | linux*|freebsd {
	CONFIG(release, debug|release):CONFIG *= Release optimize_full
	CONFIG(debug, debug|release):CONFIG *= Debug
}
contains(QT_ARCH, x86_64) {
	ARCHITECTURE = x64
} else {
	ARCHITECTURE = x86
}

android {
	Release:OUTPUT_DIR=android/release
	Debug:OUTPUT_DIR=android/debug

} else:ios {
	Release:OUTPUT_DIR=ios/release
	Debug:OUTPUT_DIR=ios/debug

} else {
	Release:OUTPUT_DIR=release/$${ARCHITECTURE}
	Debug:OUTPUT_DIR=debug/$${ARCHITECTURE}
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

win32*:!*msvc2012:*msvc* {
	QMAKE_CXXFLAGS += /FS
}

INCLUDEPATH += \
	./ \
	../cpp-template-utils/ \
	cpp-template-utils/ #for building tests in CI workflows

win*{
	QMAKE_CXXFLAGS += /MP /Zi /JMC
	QMAKE_CXXFLAGS += /std:c++latest /permissive- /Zc:__cplusplus /Zc:char8_t
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX
	QMAKE_CXXFLAGS_WARN_ON = /W4

	!*msvc2013*:QMAKE_LFLAGS += /DEBUG:FASTLINK

	Debug:QMAKE_LFLAGS += /INCREMENTAL
	Release:QMAKE_LFLAGS += /OPT:REF /OPT:ICF
}

linux*|mac*|freebsd{
	QMAKE_CXXFLAGS += -std=c++2b
	QMAKE_CXXFLAGS += -pedantic-errors
	QMAKE_CFLAGS += -pedantic-errors

	QMAKE_CXXFLAGS_WARN_ON = -Wall -Wextra -Werror=duplicated-cond -Werror=duplicated-branches -Warith-conversion -Warray-bounds -Wattributes -Wcast-align -Wcast-qual -Wconversion -Wdate-time -Wduplicated-branches -Wendif-labels -Werror=overflow -Werror=return-type -Werror=shift-count-overflow -Werror=sign-promo -Werror=undef -Wextra -Winit-self -Wlogical-op -Wmissing-include-dirs -Wnull-dereference -Wpedantic -Wpointer-arith -Wredundant-decls -Wshadow -Wstrict-aliasing -Wstrict-aliasing=3 -Wuninitialized -Wunused-const-variable=2 -Wwrite-strings -Wlogical-op
	QMAKE_CXXFLAGS_WARN_ON += -Wno-missing-include-dirs -Wno-undef

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}

g++*: QMAKE_CXXFLAGS += -fconcepts

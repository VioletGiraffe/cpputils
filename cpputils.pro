TEMPLATE = lib
TARGET = cpputils

CONFIG += staticlib
CONFIG -= qt

CONFIG += strict_c++ c++14

mac* | linux*{
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
include (compiler/compiler.pri)
include (assert/assert.pri)
include (lang/lang.pri)
include (regex/regex.pri)

win32*:!*msvc2012:*msvc* {
	QMAKE_CXXFLAGS += /FS
}

INCLUDEPATH += \
	./ \
	../cpp-template-utils/

win*{
	QMAKE_CXXFLAGS += /MP /Zi
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX
	QMAKE_CXXFLAGS_WARN_ON = /W4

	!*msvc2013*:QMAKE_LFLAGS += /DEBUG:FASTLINK

	Debug:QMAKE_LFLAGS += /INCREMENTAL
	Release:QMAKE_LFLAGS += /OPT:REF /OPT:ICF
}

linux*|mac*{
	QMAKE_CXXFLAGS += -pedantic-errors
	QMAKE_CFLAGS += -pedantic-errors
	QMAKE_CXXFLAGS_WARN_ON = -Wall -Wno-c++11-extensions -Wno-local-type-template-args -Wno-deprecated-register

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}

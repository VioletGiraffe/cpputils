CONFIG += strict_c++ c++2a
CONFIG -= qt

TEMPLATE = app
CONFIG += console
DESTDIR = $${PWD}/../bin

mac* | linux*|freebsd {
	CONFIG(release, debug|release):CONFIG *= Release optimize_full
	CONFIG(debug, debug|release):CONFIG *= Debug
}

contains(QT_ARCH, x86_64) {
	ARCHITECTURE = x64
} else {
	ARCHITECTURE = x86
}

Release:OUTPUT_DIR=release/$${ARCHITECTURE}
Debug:OUTPUT_DIR=debug/$${ARCHITECTURE}

win*{
	QMAKE_CXXFLAGS += /std:c++latest /permissive- /Zc:__cplusplus

	QMAKE_CXXFLAGS += /MP /Zi /FS
	QMAKE_CXXFLAGS += /wd4251
	QMAKE_CXXFLAGS_WARN_ON = /W4
}

linux*|mac*{
	QMAKE_CXXFLAGS_WARN_ON = -Wall
	QMAKE_CXXFLAGS += -std=c++2a

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}

*g++*:QMAKE_CXXFLAGS += -fconcepts

INCLUDEPATH += \
	$${PWD}/../../ \ #self
	$${PWD}/../../../cpp-template-utils \
	$${PWD}/../../cpp-template-utils #same, but for CI

LIBS += \
	-L../../../bin/$${OUTPUT_DIR} \ #self
	-Lbin/$${OUTPUT_DIR} \ #same, but for CI
	-lcpputils

SOURCES += \
	main.cpp \
	thread_pool_tests.cpp

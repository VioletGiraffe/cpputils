CONFIG += strict_c++ c++latest
CONFIG -= qt

TEMPLATE = app
CONFIG += console
DESTDIR = $${PWD}/../bin

mac* | linux*|freebsd {
	CONFIG(release, debug|release):CONFIG *= Release optimize_full
	CONFIG(debug, debug|release):CONFIG *= Debug
}

Release:OUTPUT_DIR=release
Debug:OUTPUT_DIR=debug

win*{
	QMAKE_CXXFLAGS += /std:c++latest /permissive- /Zc:__cplusplus

	QMAKE_CXXFLAGS += /MP /Zi /FS
	QMAKE_CXXFLAGS += /wd4251
	QMAKE_CXXFLAGS_WARN_ON = /W4

	Debug:QMAKE_LFLAGS += /DEBUG:FASTLINK /INCREMENTAL

	Release:QMAKE_CXXFLAGS += /GL
	Release:QMAKE_LFLAGS += /DEBUG:FULL /OPT:REF /OPT:ICF /TIME /LTCG:INCREMENTAL
}

linux*|mac*{
	QMAKE_CXXFLAGS += -std=c++2b
	QMAKE_CXXFLAGS_WARN_ON = -Wall

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}

*g++*:QMAKE_CXXFLAGS += -fconcepts

DEFINES += CATCH_CONFIG_ENABLE_BENCHMARKING

INCLUDEPATH += \
	$${PWD}/../../ \ #self
	$${PWD}/../../../cpp-template-utils \
	$${PWD}/../../cpp-template-utils #same, but for CI

LIBS += \
	-L$${PWD}/../../../bin/$${OUTPUT_DIR} \ #self
	-Lbin/$${OUTPUT_DIR} \ #same, but for CI
	-lcpputils

SOURCES += \
	execution_queue_tests.cpp \
	main.cpp \
	thread_pool_tests.cpp

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
	QMAKE_CXXFLAGS += /std:c++latest /permissive- /Zc:__cplusplus /utf-8

	QMAKE_CXXFLAGS += /MP /Zi /FS
	QMAKE_CXXFLAGS_WARN_ON = /W4 /wd4251

	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX

	QMAKE_LFLAGS += /DEBUG
	Debug:QMAKE_LFLAGS += /INCREMENTAL

	Release:QMAKE_CXXFLAGS += /GL
	Release:QMAKE_LFLAGS += /OPT:REF /OPT:ICF /TIME /LTCG:INCREMENTAL
}

linux*|mac*|freebsd{
	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}

linux*:Release {
	QMAKE_CXXFLAGS += -flto=auto
	QMAKE_LFLAGS   += -flto=auto
}

mac*:Release {
	QMAKE_CXXFLAGS += -flto=thin
	QMAKE_LFLAGS   += -flto=thin
}

*g++*:QMAKE_CXXFLAGS += -fconcepts
*g++*:QMAKE_CXXFLAGS_WARN_ON += -Wno-maybe-uninitialized # False positives on std::optional and std::expected

mac*{
	QMAKE_MACOSX_DEPLOYMENT_TARGET = 13.3
}

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
	main.cpp \
	cinterruptablethread_tests.cpp \
	execution_queue_tests.cpp \
	memory_functions_tests.cpp \
	thread_pool_tests.cpp

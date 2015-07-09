TEMPLATE = lib
CONFIG += staticlib
TARGET = cpputils

DESTDIR  = ../bin
OBJECTS_DIR = ../build/cpputils/
MOC_DIR     = ../build/cpputils/
UI_DIR      = ../build/cpputils/
RCC_DIR     = ../build/cpputils/

CONFIG -= qt
CONFIG += c++14

include (system/system.pri)
include (math/math.pri)
include (threading/threading.pri)
include (compiler/compiler.pri)
include (assert/assert.pri)

INCLUDEPATH += ./

win*{
    QMAKE_CXXFLAGS += /MP
    DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX
    QMAKE_CXXFLAGS_WARN_ON = -W4
}

linux*|mac*{
    QMAKE_CXXFLAGS += -pedantic-errors
    QMAKE_CFLAGS += -pedantic-errors
    QMAKE_CXXFLAGS_WARN_ON = -Wall -Wno-c++11-extensions -Wno-local-type-template-args -Wno-deprecated-register

    CONFIG(release, debug|release):CONFIG += Release
    CONFIG(debug, debug|release):CONFIG += Debug

    Release:DEFINES += NDEBUG=1
    Debug:DEFINES += _DEBUG
}

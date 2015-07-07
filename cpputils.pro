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

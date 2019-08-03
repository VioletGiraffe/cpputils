HEADERS += \
    $$PWD/cconsumerblockingqueue.h \
    $$PWD/cexecutionqueue.h \
    $$PWD/cperiodicexecutionthread.h \
    $$PWD/cworkerthread.h \
    $$PWD/cinterruptablethread.h \
    $$PWD/thread_helpers.h

SOURCES += \
    $$PWD/cperiodicexecutionthread.cpp \
    $$PWD/cworkerthread.cpp \
    $$PWD/cinterruptablethread.cpp \
    $$PWD/thread_helpers.cpp

freebsd {
    LIBS += -lpthread
}
HEADERS += \
	$$PWD/ctimeelapsed.h \
	$$PWD/processfilepath.hpp \
	$$PWD/consoleapplicationexithandler.h \
	$$PWD/timing.h \
	$$PWD/win_utils.hpp \
	$$PWD/storagespeed.hpp

SOURCES += \
	$$PWD/processfilepath.cpp \
	$$PWD/consoleapplicationexithandler.cpp \
	$$PWD/timing.cpp \
	$$PWD/storagespeed.cpp

win*{
	SOURCES += \
		$$PWD/win_utils.cpp
}

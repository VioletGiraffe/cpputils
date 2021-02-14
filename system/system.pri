HEADERS += \
	$$PWD/ctimeelapsed.h \
	$$PWD/processfilepath.hpp \
	$$PWD/consoleapplicationexithandler.h \
	$$PWD/win_utils.hpp

SOURCES += \
	$$PWD/ctimeelapsed.cpp \
	$$PWD/processfilepath.cpp \
	$$PWD/consoleapplicationexithandler.cpp

win*{
	SOURCES += \
		$$PWD/win_utils.cpp
}

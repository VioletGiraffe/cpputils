# Link-time dependencies of cpputils. A static lib cannot propagate LIBS through qmake, so consuming
# top-level projects include() this file instead of tracking the library's needs by hand.
# Windows and Linux currently need nothing beyond the default toolchain libs.

mac*{
	# storagespeed.cpp
	LIBS += -framework IOKit -framework CoreFoundation
}

HEADERS += \
	$$PWD/murmurhash3.h \
	$$PWD/sha3.h \ #From https://github.com/brainhub/SHA3IUF
    $$PWD/fnv_1a.h \
	$$PWD/sha3_hasher.hpp

SOURCES += \
	$$PWD/murmurhash3.cpp \
	$$PWD/sha3.c #From https://github.com/brainhub/SHA3IUF

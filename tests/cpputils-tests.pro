TEMPLATE = subdirs

SUBDIRS = tests cpputils

cpputils.file = ../cpputils.pro
tests.file = test-app/test-app.pro
tests.depends = cpputils

TEMPLATE = subdirs

SUBDIRS = cpputils tests

cpputils.file = ../cpputils.pro
tests.file = test-app/test-app.pro
tests.depends = cpputils

QT += core gui qml quick quickcontrols2 testlib dbus network
CONFIG += c++17 testcase
TARGET = lectern-omarchy_tests
INCLUDEPATH += ../src
HEADERS += ../src/updater.h ../src/backend.h ../src/deck.h ../src/slideview.h ../src/portal.h ../src/theme.h ../src/translator.h
SOURCES += ../src/updater.cpp lectern-omarchy_tests.cpp ../src/backend.cpp ../src/deck.cpp ../src/slideview.cpp ../src/portal.cpp ../src/theme.cpp ../src/translator.cpp
RESOURCES += ../src/resources.qrc
HEADERS += ../src/editorprojection.h
SOURCES += ../src/editorprojection.cpp
LIBS += -lmd4c
include(../src/presentationidle.pri)

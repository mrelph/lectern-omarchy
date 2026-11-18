QT += core gui qml quick quickcontrols2 dbus network
CONFIG += c++17 release
VERSION = $$cat($$PWD/VERSION)
DEFINES += LECTERN_VERSION=\\\"$$VERSION\\\"
TARGET = lectern-omarchy
HEADERS += src/updater.h src/backend.h src/deck.h src/slideview.h src/portal.h src/theme.h src/translator.h
SOURCES += src/updater.cpp src/main.cpp src/backend.cpp src/deck.cpp src/slideview.cpp src/portal.cpp src/theme.cpp src/translator.cpp
RESOURCES += src/resources.qrc

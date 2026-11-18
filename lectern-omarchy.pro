QT += core gui qml quick quickcontrols2 dbus
CONFIG += c++17 release
TARGET = lectern-omarchy
HEADERS += src/backend.h src/deck.h src/slideview.h src/portal.h src/theme.h src/translator.h
SOURCES += src/main.cpp src/backend.cpp src/deck.cpp src/slideview.cpp src/portal.cpp src/theme.cpp src/translator.cpp
RESOURCES += src/resources.qrc

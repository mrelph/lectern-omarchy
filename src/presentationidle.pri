QT += gui-private waylandclient
CONFIG += link_pkgconfig
PKGCONFIG += wayland-client
protocolDir = $$system(pkg-config --variable=pkgdatadir wayland-protocols)
WAYLANDCLIENTSOURCES += $$protocolDir/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml
load(wayland-scanner)
HEADERS += $$PWD/presentationidle.h
SOURCES += $$PWD/presentationidle.cpp

TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += main.cpp

CONFIG += c++17 \
c++14\

LIBS += \
-lboost_program_options\
-lcrypto\
-lpthread\
-lstdc++fs\

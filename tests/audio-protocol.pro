TEMPLATE = app
TARGET = AudioProtocolTests
QT -= core gui
CONFIG += console release
CONFIG -= app_bundle debug debug_and_release
include(audio-common.pri)
SOURCES += test_audio_protocol.c test_audio_receive.c
DEFINES += LI_AUDIO_PROTOCOL_TEST_MAIN

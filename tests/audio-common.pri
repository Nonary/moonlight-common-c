# Compile the real receiver with only its local synchronous replay adapter enabled.
# No second parser, queue, crypto implementation or network sender is linked.
COMMON_AUDIO_ROOT = $$clean_path($$PWD/..)
INCLUDEPATH += $$COMMON_AUDIO_ROOT/src $$COMMON_AUDIO_ROOT/tests \
               $$COMMON_AUDIO_ROOT/enet/include $$COMMON_AUDIO_ROOT/nanors \
               $$COMMON_AUDIO_ROOT/nanors/deps $$COMMON_AUDIO_ROOT/nanors/deps/obl
DEFINES += LI_AUDIO_TESTING HAS_SOCKLEN_T
SOURCES += $$files($$COMMON_AUDIO_ROOT/src/*.c) $$files($$COMMON_AUDIO_ROOT/enet/*.c) \
           $$COMMON_AUDIO_ROOT/nanors/rs.c \
           $$COMMON_AUDIO_ROOT/nanors/deps/obl/oblas_common.c \
           $$COMMON_AUDIO_ROOT/nanors/deps/obl/oblas_lite.c
win32 {
    COMMON_APP_ROOT = $$clean_path($$COMMON_AUDIO_ROOT/../..)
    INCLUDEPATH += $$COMMON_APP_ROOT/libs/windows/include/x64
    LIBS += -L$$COMMON_APP_ROOT/libs/windows/lib/x64 -llibcrypto -lws2_32 -lwinmm
    DEFINES += HAS_QOS_FLOWID=1 HAS_PQOS_FLOWID=1
    QMAKE_CFLAGS_WARN_ON = /W3
    # C5287 is pre-existing enum mixing in vendored ENet, newly warned by MSVC 14.44.
    QMAKE_CFLAGS += /WX /wd4100 /wd4232 /wd5105 /wd5287
    contains(CONFIG, spatial_analyze): QMAKE_CFLAGS += /analyze
} else {
    CONFIG += link_pkgconfig
    PKGCONFIG += openssl
    DEFINES += HAVE_CLOCK_GETTIME=1
    QMAKE_CFLAGS += -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter
    LIBS += -lpthread
}

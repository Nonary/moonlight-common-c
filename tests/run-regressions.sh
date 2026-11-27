#!/bin/sh
# Host tests of the Steam Controller (2026) protocol extensions. They compile the
# production source under test into the test binary and link the rest from a
# CMake build of the library (shared on Linux/macOS, the import library on MinGW).
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${1:?Usage: tests/run-regressions.sh path/to/cmake-build}
build=$(CDPATH= cd -- "$build" && pwd)
cc=${CC:-cc}
# System libraries the production sources under test call directly (sockets on Windows).
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) syslibs="-lws2_32 -lwinmm" ;; *) syslibs= ;; esac
# Match sanitizer flags used to build the library via CFLAGS, when applicable.
for test in steam_haptic steam_settings steam_attributes controller_batching control_truncation input_motion host_cursor; do
    extra_flags=
    # Exercise the input producer assertions in debug builds.
    if [ "$test" = controller_batching ]; then extra_flags=-DLC_DEBUG; fi
    "$cc" ${CFLAGS:-} $extra_flags -ffunction-sections -fdata-sections -DHAS_SOCKLEN_T \
        -I"$repo/src" -I"$repo/enet/include" -I"$repo/nanors" -I"$repo/nanors/deps" -I"$repo/nanors/deps/obl" \
        "$repo/tests/test_$test.c" \
        -L"$build" -lmoonlight-common-c $syslibs -Wl,--gc-sections -Wl,-rpath,"$build" \
        -pthread -o "$build/test-$test"
done
"$build/test-steam_haptic"
"$build/test-steam_settings"
"$build/test-steam_attributes"
"$build/test-controller_batching"
"$build/test-control_truncation"
"$build/test-input_motion"
"$build/test-host_cursor"

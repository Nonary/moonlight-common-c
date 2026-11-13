# Discrete 7.1.4 protocol and receiver regression tests

The client keeps the Nonary Common baseline
`3f9c009fe01db573cd29c1f286db5b7fa8a922fc`; it does not replace it with
Foundation's Common fork. App and Common must be rebuilt together:
`OPUS_MULTISTREAM_CONFIGURATION.mapping` grows from 8 to 12 bytes (the public
struct grows from 28 to 32 bytes). The `STREAM_CONFIGURATION` layout is unchanged.

## Negotiation contract

`AUDIO_CONFIGURATION_714_SURROUND` packs to unsigned `0xF63F0CCA`.
`surroundAudioInfo` is unsigned `0xF63F000C`, decimal `4131323916`.
`0xF63F` is a 16-bit Foundation wire identifier, never a Windows speaker mask.
Windows WAVE uses `0x2D63F`; Windows Spatial uses the SDK object flags, expected
to combine to `0x1FFE` for this layout.

The accepted new normal profile is 48 kHz, 12 channels, 8 streams, 4 coupled,
identity mapping 0..11, in this order:
`FL FR FC LFE BL BR SL SR TFL TFR TBL TBR`. No legacy LFE rotation applies.
An optional following 12/12/0 identity offer is validated but does not enable
HQ. Video bitrate and renderer duration flags never request HQ or 10 ms for
12 channels. The negotiated duration stays 5 ms / 240 samples per channel.
No bitrate limit is imposed by 12/8/4 or by the decoder.

`AudioConfig.c` is the production parser called by `RtspConnection.c`, using
the actual response payload length. It reads bounded lines and complete tokens,
checks 8-bit decimal overflow, Opus stream relationships and mapping entries,
and rejects missing/extra tokens, negative numbers, embedded NULs, unsupported
12-channel mappings and inconsistent normal/HQ order. A complete final line
without a newline is accepted. A surround line is limited to 256 bytes.
Identical 12-channel offers are harmless repeats; conflicting ones fail.
An absent or invalid 12-channel offer returns
`ML_ERROR_UNSUPPORTED_AUDIO_CONFIGURATION` (-105) before audio renderer init.

The complete bounded channel field is selected before validating mappings;
unrequested custom/future profiles never impose discrete-12 restrictions on
legacy sessions. In particular 120 cannot be mistaken for 12.

Legacy packed single-digit fields and the historical first-two-offer policy
remain supported for 2/6/8 channels, including custom duplicate offers, duplicate
or silence mappings permitted by Opus, and the old 5.1 fallback when no offer is
present. Normal 6/8 mappings receive the existing single LFE rotation; their HQ
offers do not. Stereo continues to use its fixed 1/1 mapping independently of
surround attributes.

`fixtures/foundation_audio_sdp.h` records the standard surround-attribute block
from pinned Foundation's actual `cmd_describe()` behavior. The tests prepend the
two custom offers that Foundation advertises before that block, verifying both
the new duplicate handling and the legacy first-two behavior.

## Actual UDP budget

`RTPA_MAX_PACKET_SIZE` remains 1400 bytes for the entire UDP payload.
The fixed RTP header is 12 bytes. Data datagrams contain RTP + shard; FEC
datagrams contain RTP + 12-byte audio FEC header + parity shard. Four data and
two parity shards protect a 20 ms block at a 5 ms packet duration.

For Opus payload `P` bytes, the unencrypted shard is `P`. With AES-CBC PKCS7 it
is `C = 16 * (floor(P / 16) + 1)`, including a full padding block when `P` is a
multiple of 16. FEC operates on the ciphertext. Therefore a FEC-safe shard is
at most 1376 bytes, corresponding to at most 1376 raw Opus bytes without
encryption or **1375 raw Opus bytes with encryption**. An unencrypted data-only
datagram can carry 1388 bytes, but its matching FEC datagram would not fit.
The receiver accepts valid data up to its datagram boundary; it does not infer
an encoder bitrate cap from these sizes.

| Target at 5 ms | Opus bytes | Clear data / FEC | CBC shard | Encrypted data / FEC |
|---|---:|---:|---:|---:|
| 600 kbit/s | 375 | 387 / 399 | 384 | 396 / 408 |
| 1536 kbit/s | 960 | 972 / 984 | 976 | 988 / 1000 |
| 2048 kbit/s | 1280 | 1292 / 1304 | 1296 | 1308 / 1320 |
| 3072 kbit/s | 1920 | 1932 / 1944 | 1936 | 1948 / 1960 |

These are CBR payload calculations; actual encoded packet lengths must still
be checked. Even a 1400-byte codec output would require a 1424-byte clear FEC
datagram or a 1432-byte encrypted FEC datagram. A codec output buffer is not a
free UDP payload budget. No receive buffer increase, fragmentation scheme or
2.5 ms protocol change is introduced.

IPv4 UDP/IP adds 28 bytes and IPv6 UDP/IP adds 48 bytes beyond the table's UDP
datagrams. Ethernet framing is another layer. FEC and those outer headers also
consume link bandwidth; the nominal Opus bitrate is not total link bitrate.

## Build and run

From an x64 MSVC developer shell, with the parent app's existing dependencies:

```bat
mkdir build-audio
cd build-audio
qmake ..\tests\audio-protocol.pro CONFIG+=release
nmake
set PATH=..\..\..\libs\windows\lib\x64;%PATH%
AudioProtocolTests.exe
```

The relative `PATH` example assumes the Common checkout remains nested at
`moonlight-common-c/moonlight-common-c` in the app. Alternatively put the
existing `libcrypto-3-x64.dll` beside the executable. Qt runtime DLLs are not
needed by this Common-only test executable. The app's separate codec transport
suite includes `audio-common.pri` to exercise the same receiver with real Opus.

Portable CMake entry point (requires existing OpenSSL and compiler):

```sh
cmake -S . -B build-audio -DBUILD_SHARED_LIBS=OFF -DBUILD_AUDIO_REGRESSION_TESTS=ON
cmake --build build-audio
ctest --test-dir build-audio --output-on-failure
```

On a machine with working GCC/Clang sanitizer runtimes, add
`-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"`
and `-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"` at configure time.
This entry point was prepared but Linux/UBSan execution was unavailable on the
Windows validation machine; it is not reported as a passing sanitizer run.

The parser-only sanitizer build uses exactly the same production `AudioConfig.c`
and protocol test source, excluding generated-SDP and crypto replay tests:

```bat
cl /nologo /W4 /WX /fsanitize=address /Z7 /Od /MD ^
  /DLI_AUDIO_PROTOCOL_TEST_MAIN /DLI_AUDIO_PARSER_ONLY ^
  tests\test_audio_protocol.c src\AudioConfig.c /Isrc ^
  /Fe:AudioParserAsan.exe /link /INCREMENTAL:NO /DEBUG:NONE
```

Use the installed x86 MSVC environment when only its x86 ASan runtime is
available. Put that runtime directory on `PATH`. `/DEBUG:NONE` avoids the local
PDB-manager failure; MSVC then emits LNK4302 about reduced ASan symbolization.
Memory instrumentation remains enabled. No compiler or sanitizer was installed
as part of this work.

## Validated coverage, 2026-11-03

Windows x64 Release: Qt qmake 6.11.1, MSVC 14.44.35207, SDK 10.0.26100.0,
existing app OpenSSL DLL. All production Common sources build in the test
executable with `/W3 /WX`. The only additional warning exclusion is MSVC
C5287 for pre-existing enum mixing in vendored ENet.

| Run | Cases | Individual assertions | Failures | SKIP |
|---|---:|---:|---:|---:|
| Protocol, generated SDP, wire packing | 127 | 655 | 0 | 0 |
| Actual AudioStream / RTP queue / FEC / decrypt replay | 19 | 17627 | 0 | 0 |
| Same parser/wire fixtures, x86 MSVC AddressSanitizer | 112 | 571 | 0 | 0 |

Not executed (SKIP): Linux ASan/UBSan and full x64 receiver ASan, because the
corresponding local toolchain/runtime is unavailable. Manual host, device and
physical playback checks are outside this offline Common suite and remain open.

Protocol cases include every allocated short prefix of the new normal line,
12/120 prefix confusion, integer overflow, malformed line/token endings,
identity and LFE/height positions, old and new standard fixtures, custom
duplicate fixtures, legacy silence/duplicates, unsigned top-bit serialization,
and 2/6/8 normal/HQ bitrate/decoder-duration regressions.

Replay calls the real socket receiver's shared `submitAudioPacket()` path,
`RtpaAddPacket()`, `RtpaGetQueuedPacket()`, `decodeInputData()` and production
crypto. It validates exact binary payload bytes, ordering, repeated data/parity,
one/two-loss FEC recovery, unrecoverable loss/NULL PLC callbacks, late packets,
sequence wrap, every invalid RTP first byte, runt/oversized datagrams, invalid
FEC metadata and mismatching block identity, exact 1400-byte FEC datagrams,
invalid CBC lengths, deterministic invalid PKCS7 padding and next-packet decrypt
recovery. This exposed and fixed a production gap-closing bug: buffered OOS
packets are now drained immediately after a fast-path in-order packet too.

The replay adapter exists only under `LI_AUDIO_TESTING`; it creates no sockets,
threads or audio device. It bypasses the socket read, startup backlog timer and
blocking-queue decoder-thread scheduling. It does not prove real network timing,
real host interoperation, renderer object activation or physical height sound.
Binary payload comparisons are transport tests, not PCM-byte-identity claims.
Actual Opus decoding/correlation and renderer tests belong to the parent app's
separate suites. No hardware playback is performed by these tests.

MSVC `/analyze` was run on all six changed production C translation units, with
SDK/CRT external headers excluded. No findings appeared in AudioConfig,
AudioStream or RtspConnection. Six diagnostics in unchanged regions of the other
three files were reproduced against the pinned baseline: RtpAudioQueue C6001
(two), C28182 and C6385; SdpGenerator C6340 at its existing configured-video-
bitrate formatting; Connection C6244 at its existing stage-name parameter.
These are baseline findings, not a clean-analysis claim. SDK ws2tcpip.h C6101
was separately observed before excluding external SDK headers.

## Source pins

- [Foundation audio profiles and 1400-byte codec buffer](https://github.com/AlkaidLab/foundation-sunshine/blob/997ef4500468a703d8fe30abe14daf1549e35b85/src/audio.cpp)
- [Foundation DESCRIBE custom/standard offers and 12-channel default](https://github.com/AlkaidLab/foundation-sunshine/blob/997ef4500468a703d8fe30abe14daf1549e35b85/src/rtsp.cpp)
- [Foundation RTP, CBC then audio parity, wire timestamp in milliseconds](https://github.com/AlkaidLab/foundation-sunshine/blob/997ef4500468a703d8fe30abe14daf1549e35b85/src/stream.cpp)
- [Foundation channel order](https://github.com/AlkaidLab/foundation-sunshine/blob/997ef4500468a703d8fe30abe14daf1549e35b85/src/platform/common.h)
- [Foundation Common wire identifier](https://github.com/skyhua0224/moonlight-common-c/blob/3ccd50806d2bb0446db61b88501b6f7609daa89d/src/Limelight.h)

Only the discrete Opus profile and wire identifier are adopted. No DRED,
passthrough codecs, MAT path, host capture, driver or Foundation fork replacement
is part of this Common change.

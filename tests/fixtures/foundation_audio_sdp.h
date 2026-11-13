// Surround-attribute block produced by Foundation cmd_describe()'s standard
// stream_configs loop. Pins: AlkaidLab/foundation-sunshine
// 997ef4500468a703d8fe30abe14daf1549e35b85, src/rtsp.cpp:1350-1389,
// src/audio.cpp:276-345 and src/platform/common.h:244-296.
// The normal 6/8 mappings rotate LFE left on the wire; 12 does not.
static const char foundationAudioSdp[] =
    "a=fmtp:97 surround-params=21101\n"
    "a=fmtp:97 surround-params=21101\n"
    "a=fmtp:97 surround-params=642012453\n"
    "a=fmtp:97 surround-params=660012345\n"
    "a=fmtp:97 surround-params=85301245673\n"
    "a=fmtp:97 surround-params=88001234567\n"
    "a=fmtp:97 surround-params=12,8,4,0,1,2,3,4,5,6,7,8,9,10,11\n"
    "a=fmtp:97 surround-params=12,12,0,0,1,2,3,4,5,6,7,8,9,10,11\n";

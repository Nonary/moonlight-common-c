#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
// Available only when AudioStream.c is built with LI_AUDIO_TESTING.
void LiTestAudioBegin(bool encrypted, const unsigned char key[16], const unsigned char iv[16],
                      void (*decode)(char*, int));
int LiTestAudioPush(const unsigned char* wireDatagram, int length);
void LiTestAudioEnd(void);
int runAudioProtocolTests(void);
int runAudioReceiveTests(void);
#ifdef __cplusplus
}
#endif

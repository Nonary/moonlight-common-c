// SPDX-License-Identifier: GPL-3.0-or-later
// GPL-3.0-or-later, like moonlight-common-c. Original C regression fixture.
// Timeline inspired by RamazanKara/nightjar c459987a7c5479a6561ca6b2bbf2955662f0f056,
// protocol/client/src/audio/tests.rs and queue.rs (Moonlight contributors).
// The real queue is linked unchanged except the candidate's units correction.
// Only this offline fixture ages the queue timestamp. No sleep/timer scheduling
// uncertainty and no production time-source or security-check replacement.
#include "Limelight-internal.h"
#include "RtpAudioQueue.h"
#include "rs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int assertions, failures, plc, delivered, inorder_failures;
static void quiet_log(const char* format, ...) {(void)format;}
#define CHECK(x) do { assertions++; if(!(x)){failures++;fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);} }while(0)
static void drain(PRTP_AUDIO_QUEUE q) {
    uint16_t length; PRTP_PACKET p;
    while((p=RtpaGetQueuedPacket(q,1,&length))!=NULL){
        // Loss entries have no payload; normal fixture packets have 64 bytes.
        if(length==0)plc++;else {CHECK(length==sizeof(RTP_PACKET)+64);delivered++;}
        free(p);
    }
}
static int push(PRTP_AUDIO_QUEUE q,int seq) {
    union {uint64_t align; unsigned char bytes[sizeof(RTP_PACKET)+64];} u;
    memset(&u,0,sizeof(u)); PRTP_PACKET p=(PRTP_PACKET)u.bytes;
    p->header=0x80;p->packetType=97;p->sequenceNumber=(uint16_t)seq;p->timestamp=(uint32_t)(seq*AudioPacketDuration);p->ssrc=0x714;
    for(unsigned i=0;i<64;i++)u.bytes[sizeof(RTP_PACKET)+i]=(unsigned char)(seq^i);
    int r=RtpaAddPacket(q,p,sizeof(RTP_PACKET)+64);
    if(RTPQ_HANDLE_NOW(r))delivered++;
    // AudioStream also drains after direct submit: the late in-order packet
    // can unlock previously queued successors without another datagram.
    drain(q);
    return r;
}
static void begin(PRTP_AUDIO_QUEUE q,int duration,int reordered) {
    AudioPacketDuration=duration;AudioEncryptionEnabled=false;
    AppVersionQuad[0]=7;AppVersionQuad[1]=1;AppVersionQuad[2]=431;AppVersionQuad[3]=-1;
    plc=delivered=0;RtpaInitializeQueue(q);push(q,0);
    for(int n=4;n<8;n++)push(q,n);
    CHECK(delivered==4);
    if(reordered){push(q,1);CHECK(q->receivedOosData);}
    push(q,8);push(q,9);push(q,11);CHECK(q->blockHead!=NULL);
}
static void deadline_case(int duration,int age_us,int reordered,int expect_loss) {
    RTP_AUDIO_QUEUE q;begin(&q,duration,reordered);
    CHECK(q.blockHead->fecHeader.baseSequenceNumber==8);
    q.blockHead->queueTimeUs=PltGetMicroseconds()-(uint64_t)age_us;
    push(&q,12);
    CHECK(q.stats.packetCountFecFailed==(unsigned)expect_loss);
    if(!expect_loss){CHECK(plc==0);push(&q,10);CHECK(plc==0);CHECK(delivered==9);}
    else CHECK(plc==1);
    RtpaCleanupQueue(&q);
}
static void fresh_start(int advertised, unsigned base, int reordered) {
    RTP_AUDIO_QUEUE q;
    AudioPacketDuration=5; AudioEncryptionEnabled=false;
    AppVersionQuad[0]=7;AppVersionQuad[1]=1;AppVersionQuad[2]=431;AppVersionQuad[3]=-1;
    FreshAudioAfterPing=(advertised!=0);
    RtpaInitializeQueue(&q);
    plc=delivered=0;
    const int order[]={reordered?1:0,reordered?0:1,2,3};
    for(unsigned i=0;i<4;++i)push(&q,(uint16_t)(base+order[i]));
    CHECK(!plc);
    CHECK(delivered==((advertised && base==0)?4:0));
    for(unsigned i=4;i<8;++i)push(&q,(uint16_t)(base+i));
    CHECK(delivered==((advertised && base==0)?8:4));
    CHECK(!plc);
    RtpaCleanupQueue(&q);
    FreshAudioAfterPing=false;
}
int main(void) {
    ListenerCallbacks.logMessage=quiet_log;
    if(initializePlatform()!=0)return 2;
    reed_solomon_init();
    // Warm up QueryPerformanceCounter and allocator; timing assertions have
    // several milliseconds of margin rather than depending on a real sleep.
    (void)PltGetMicroseconds();
    for(int flag=0;flag<=1;++flag)for(int oos=0;oos<=1;++oos){
        fresh_start(flag,0,oos);fresh_start(flag,8,oos);fresh_start(flag,65532,oos);
    }
    for(int duration=5;duration<=20;duration*=2){
        deadline_case(duration,duration*4*1000+5000,1,0);
        deadline_case(duration,duration*4*1000+15000,1,1);
        deadline_case(duration,1000,0,1);
        int before_inorder=failures;
        RTP_AUDIO_QUEUE q;AudioPacketDuration=duration;RtpaInitializeQueue(&q);plc=delivered=0;push(&q,0);
        for(int seq=4;seq<100;seq++){int before=delivered;int r=push(&q,seq);CHECK(RTPQ_HANDLE_NOW(r));CHECK(delivered==before+1);}
        CHECK(plc==0);RtpaCleanupQueue(&q);inorder_failures+=failures-before_inorder;
    }
    cleanupPlatform();
    printf("{\"schema\":\"common-fec-deadline-test/1\",\"assertions\":%d,\"failures\":%d,\"inorder_failures\":%d,\"normal_inorder_unchanged\":%s,\"packet_durations_ms\":[5,10,20],\"passed\":%s}\n",assertions,failures,inorder_failures,inorder_failures?"false":"true",failures?"false":"true");
    return failures?1:0;
}

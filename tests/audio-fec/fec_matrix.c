// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-License-Identifier: GPL-3.0-or-later
// Original regression matrix; real Common AudioStream/RTP/RS/AES receive path.
#include "Limelight-internal.h"
#include "AudioTestAdapter.h"
#include "PlatformCrypto.h"
#include "rs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef struct {unsigned char bytes[256];int length;} Packet;
static int cases,failed,immediate_failed,total_plc,checks,errors,plc,delivered;
static unsigned cursor,base;
static unsigned char key[16],initial_iv[16];
static void quiet(const char* format,...){(void)format;}
static void be16(unsigned char* p,unsigned v){p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v;}
static void be32(unsigned char* p,unsigned v){be16(p,v>>16);be16(p+2,v);}
static void plain(unsigned seq,unsigned char* out){for(unsigned i=0;i<64;i++)out[i]=(unsigned char)((seq*17u)^(seq>>8)^(i*29u));}
static void decode(char* data,int length){
 unsigned char expected[64];plain(cursor,expected);checks++;
 if(!data||length==0){plc++;}else if(length!=64||memcmp(data,expected,64)){errors++;}
 delivered++;cursor++;
}
static Packet data_packet(unsigned seq,int encrypted){
 Packet p;memset(&p,0,sizeof(p));p.bytes[0]=0x80;p.bytes[1]=97;
 be16(p.bytes+2,seq);be32(p.bytes+4,seq*5u);be32(p.bytes+8,0x714);
 unsigned char input[64];plain(seq,input);
 if(encrypted){unsigned char iv[16]={0};be32(iv,0x71400000u+(unsigned short)seq);int n=244;
  PPLT_CRYPTO_CONTEXT c=PltCreateCryptoContext(0);
  if(!c||!PltEncryptMessage(c,ALGORITHM_AES_CBC,CIPHER_FLAG_RESET_IV|CIPHER_FLAG_FINISH,key,16,iv,16,NULL,0,input,64,p.bytes+12,&n)){fprintf(stderr,"Encryption fixture failed\n");exit(3);}PltDestroyCryptoContext(c);p.length=12+n;
 }else{memcpy(p.bytes+12,input,64);p.length=76;}return p;
}
static void packets(unsigned absolute,int encrypted,Packet out[6]){
 for(int i=0;i<4;i++)out[i]=data_packet(absolute+(unsigned)i,encrypted);
 int size=out[0].length-12;unsigned char* shards[6];
 for(int i=0;i<6;i++){shards[i]=reed_solomon_aligned_alloc((size_t)reed_solomon_padded_size(size));if(!shards[i])exit(4);memset(shards[i],0,(size_t)reed_solomon_padded_size(size));if(i<4)memcpy(shards[i],out[i].bytes+12,(size_t)size);}
 reed_solomon* rs=reed_solomon_new(4,2);const unsigned char matrix[]={0x77,0x40,0x38,0x0e,0xc7,0xa7,0x0d,0x6c};memcpy(rs->p,matrix,8);
 if(reed_solomon_encode(rs,shards,6,size))exit(5);
 for(int i=0;i<2;i++){Packet* p=&out[4+i];memset(p,0,sizeof(*p));p->bytes[0]=0x80;p->bytes[1]=127;be16(p->bytes+2,absolute+4u+(unsigned)i);be32(p->bytes+4,(absolute+4u)*5u);be32(p->bytes+8,0x714);
  p->bytes[12]=(unsigned char)i;p->bytes[13]=97;be16(p->bytes+14,absolute);be32(p->bytes+16,absolute*5u);be32(p->bytes+20,0x714);memcpy(p->bytes+24,shards[4+i],(size_t)size);p->length=24+size;
 }for(int i=0;i<6;i++)reed_solomon_free(shards[i]);reed_solomon_release(rs);
}
static void push(const Packet* p){if(LiTestAudioPush(p->bytes,p->length)!=0)errors++;}
static void run(unsigned start,int encrypted,const Packet* pool,const int* order,int n,const char* kind){
 int bad=0,seen=0,unique=0,checked=0;cases++;errors=plc=delivered=0;cursor=start-4;base=start;
 LiTestAudioBegin(encrypted,key,initial_iv,decode);
 Packet p=data_packet(start-8,encrypted);push(&p);
 for(unsigned seq=start-4;seq<start;seq++){p=data_packet(seq,encrypted);push(&p);}
 if(delivered!=4||errors||plc)bad=1;
 for(int i=0;i<n;i++){push(&pool[order[i]]);if(!(seen&(1<<order[i]))){seen|=1<<order[i];unique++;}
  if(unique>=4&&!checked){checked=1;if(delivered!=8||errors||plc){bad=1;immediate_failed++;}}
 }
 if(!checked||delivered!=8||errors||plc)bad=1;
 // A duplicate from a consumed/reconstructed block must never replay data.
 int before=delivered;push(&pool[0]);if(delivered!=before)bad=1;
 // A following block exposes unnecessary PLC/stalls in the old implementation.
 for(unsigned seq=start+4;seq<start+8;seq++){p=data_packet(seq,encrypted);push(&p);}
 if(delivered!=12||errors||plc)bad=1;
 total_plc+=plc;
 if(bad){failed++;if(failed<=12){fprintf(stderr,"case=%d base=%u aes=%d kind=%s order=",cases,start,encrypted,kind);for(int i=0;i<n;i++)fprintf(stderr,"%s%d",i?",":"",order[i]);fprintf(stderr," delivered=%d plc=%d errors=%d\n",delivered,plc,errors);}}
 LiTestAudioEnd();
}
static void enumerate(unsigned start,int encrypted,const Packet* pool,int* order,int at,int length,int mask){
 if(at==length){run(start,encrypted,pool,order,length,"subset-permutation");return;}
 for(int i=0;i<6;i++)if(!(mask&(1<<i))){order[at]=i;enumerate(start,encrypted,pool,order,at+1,length,mask|(1<<i));}
}
int main(void){
 ListenerCallbacks.logMessage=quiet;if(initializePlatform())return 2;reed_solomon_init();
 for(int i=0;i<16;i++)key[i]=(unsigned char)(17+i*13);be32(initial_iv,0x71400000u);
 const unsigned starts[]={8,65532,65536};
 for(unsigned context=0;context<3;context++)for(int encrypted=0;encrypted<2;encrypted++){
  Packet pool[6];packets(starts[context],encrypted,pool);int order[6];
  for(int length=4;length<=6;length++)enumerate(starts[context],encrypted,pool,order,0,length,0);
  const int extra[][6]={{0,1,4,2,-1,-1},{0,1,5,2,-1,-1},{0,0,1,4,2,-1},{0,4,5,1,-1,-1},{4,5,0,1,-1,-1},{4,0,5,1,-1,-1}};
  for(int i=0;i<6;i++){int n=0;while(n<6&&extra[i][n]>=0)n++;run(starts[context],encrypted,pool,extra[i],n,"targeted-fastpath");}
 }
 cleanupPlatform();printf("{\"schema\":\"fec2-exhaustive-matrix/1\",\"cases\":%d,\"failed_cases\":%d,\"immediate_reconstruction_failures\":%d,\"plc_callbacks\":%d,\"payload_checks\":%d,\"contexts\":\"3 sequence bases x plaintext/AES\",\"per_context\":\"P(6,4)+P(6,5)+P(6,6)=1800 plus 6 targeted orders\",\"passed\":%s}\n",cases,failed,immediate_failed,total_plc,checks,(failed==0&&cases==10836)?"true":"false");return failed?1:0;
}

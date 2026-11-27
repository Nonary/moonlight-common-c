// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-License-Identifier: GPL-3.0-or-later
// Component microbenchmark, not source-to-speaker or network latency.
#define main matrix_fixture_main
#include "fec_matrix.c"
#undef main
#include <windows.h>
#include <stdint.h>
static LARGE_INTEGER frequency, push_start;
static double* latencies;
static unsigned samples, observed, invalid;
static void measured_decode(char* data,int length) {
 LARGE_INTEGER now;QueryPerformanceCounter(&now);
 if(!data||length!=64)invalid++;
 if(observed<samples)latencies[observed]=(double)(now.QuadPart-push_start.QuadPart)*1000000.0/(double)frequency.QuadPart;
 observed++;
}
static int cmp(const void* a,const void* b){double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
int main(int argc,char** argv){
 int aes=argc>1?atoi(argv[1]):0; samples=262144;
 Packet* pool=calloc(65536,sizeof(Packet));latencies=calloc(samples,sizeof(double));
 if(!pool||!latencies)return 2;
 ListenerCallbacks.logMessage=quiet;if(initializePlatform())return 3;reed_solomon_init();QueryPerformanceFrequency(&frequency);
 for(int i=0;i<16;i++)key[i]=(unsigned char)(17+i*13);be32(initial_iv,0x71400000u);
 for(unsigned i=0;i<65536;i++)pool[i]=data_packet(8+i,aes);
 LiTestAudioBegin(aes,key,initial_iv,measured_decode);
 Packet p=data_packet(0,aes);LiTestAudioPush(p.bytes,p.length);
 for(unsigned i=4;i<8;i++){p=data_packet(i,aes);QueryPerformanceCounter(&push_start);LiTestAudioPush(p.bytes,p.length);}
 // Warm complete cycles. Allocation and fixture generation are outside timing.
 for(unsigned i=0;i<65536;i++){QueryPerformanceCounter(&push_start);LiTestAudioPush(pool[i].bytes,pool[i].length);}
 observed=invalid=0;LARGE_INTEGER begin,end;QueryPerformanceCounter(&begin);
 for(unsigned i=0;i<samples;i++){
  Packet* packet=&pool[i&65535];QueryPerformanceCounter(&push_start);
  if(LiTestAudioPush(packet->bytes,packet->length))invalid++;
 }
 QueryPerformanceCounter(&end);LiTestAudioEnd();cleanupPlatform();
 double sum=0;for(unsigned i=0;i<samples;i++)sum+=latencies[i];qsort(latencies,samples,sizeof(double),cmp);
 printf("{\"schema\":\"fec2-inorder-component-timing/1\",\"aes\":%s,\"samples\":%u,\"callbacks\":%u,\"invalid\":%u,\"qpc_hz\":%lld,\"callback_us\":{\"mean\":%.6f,\"p50\":%.6f,\"p95\":%.6f,\"p99\":%.6f,\"max\":%.6f},\"push_loop_us_per_packet\":%.6f,\"passed\":%s}\n",
 aes?"true":"false",samples,observed,invalid,(long long)frequency.QuadPart,sum/samples,latencies[samples/2],latencies[(samples*95)/100],latencies[(samples*99)/100],latencies[samples-1],(double)(end.QuadPart-begin.QuadPart)*1000000.0/frequency.QuadPart/samples,(observed==samples&&!invalid)?"true":"false");
 free(pool);free(latencies);return observed==samples&&!invalid?0:1;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-License-Identifier: GPL-3.0-or-later
#define main matrix_fixture_main
#include "fec_matrix.c"
#undef main
int main(void){
 ListenerCallbacks.logMessage=quiet;if(initializePlatform())return 2;reed_solomon_init();
 for(int i=0;i<16;i++)key[i]=(unsigned char)(17+i*13);be32(initial_iv,0x71400000u);
 int failedProfiles=0;
 for(int aes=0;aes<2;aes++){
  errors=plc=delivered=0;cursor=4;Packet pool[6];packets(8,aes,pool);
  LiTestAudioBegin(aes,key,initial_iv,decode);Packet p=data_packet(0,aes);push(&p);
  for(unsigned i=4;i<8;i++){p=data_packet(i,aes);push(&p);}
  int order[]={0,1,4,2};const char* names[]={"data8","data9","parity0","data10"};
  for(int i=0;i<4;i++){push(&pool[order[i]]);printf("{\"aes\":%s,\"received\":\"%s\",\"target_callbacks\":%d,\"plc\":%d,\"errors\":%d}\n",aes?"true":"false",names[i],delivered-4,plc,errors);}
  int immediate=delivered==8&&!plc&&!errors;
  for(unsigned i=12;i<16;i++){p=data_packet(i,aes);push(&p);}
  int passed=immediate&&delivered==12&&!plc&&!errors;
  printf("{\"aes\":%s,\"immediate_repair\":%s,\"callbacks_after_next_block\":%d,\"plc\":%d,\"errors\":%d,\"passed\":%s}\n",aes?"true":"false",immediate?"true":"false",delivered-4,plc,errors,passed?"true":"false");
  if(!passed)failedProfiles++;LiTestAudioEnd();
 }
 cleanupPlatform();return failedProfiles?1:0;
}

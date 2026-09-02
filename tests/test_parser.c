#include "mm_elf.h"
#include "mm_mapper.h"
#include "mm_sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void w16(unsigned char *b,size_t o,uint16_t v){memcpy(b+o,&v,2);} 
static void w32(unsigned char *b,size_t o,uint32_t v){memcpy(b+o,&v,4);} 
static void w64(unsigned char *b,size_t o,uint64_t v){memcpy(b+o,&v,8);} 
static void wi64(unsigned char *b,size_t o,int64_t v){memcpy(b+o,&v,8);} 

static unsigned char *build_raw_elf(size_t *sz_out){
    const size_t sz=0x1800; unsigned char *b=calloc(1,sz); assert(b);
    b[0]=0x7f;b[1]='E';b[2]='L';b[3]='F';b[4]=2;b[5]=1;b[6]=1;
    w16(b,16,0xfe18); w16(b,18,0x3e); w32(b,20,1); w64(b,24,0x1000);
    w64(b,32,64); w16(b,52,64); w16(b,54,56); w16(b,56,2);
    /* LOAD */
    w32(b,64,MM_PT_LOAD); w32(b,68,5); w64(b,72,0x1000); w64(b,80,0x1000); w64(b,96,0x800); w64(b,104,0x800); w64(b,112,0x1000);
    /* DYNAMIC at VA 0x1100 */
    size_t p=64+56; w32(b,p,MM_PT_DYNAMIC); w32(b,p+4,4); w64(b,p+8,0x1100); w64(b,p+16,0x1100); w64(b,p+32,0x100); w64(b,p+40,0x100); w64(b,p+48,8);
    /* dynstr at VA 0x1300, dynsym at 0x1200 */
    size_t d=0x1100; 
#define D(TAG,VAL) do{wi64(b,d,(TAG));w64(b,d+8,(VAL));d+=16;}while(0)
    D(MM_DT_STRTAB,0x1300); D(MM_DT_STRSZ,64); D(MM_DT_SYMTAB,0x1200); D(MM_DT_SYMENT,24); D(MM_DT_SCE_SYMTABSZ,48); D(MM_DT_JMPREL,0x1400); D(MM_DT_PLTRELSZ,24); D(MM_DT_NEEDED,1); D(MM_DT_NULL,0);
#undef D
    /* sym 1 import name offset 14 */
    w32(b,0x1200+24,14); b[0x1200+24+4]=0x12; w16(b,0x1200+24+6,0);
    memcpy(b+0x1300,"\0libTest.sprx\0g2tViFIohHE\0",27);
    /* relocation -> symbol 1 */
    w64(b,0x1400,0x5000); w64(b,0x1408,((uint64_t)1<<32)|7); w64(b,0x1410,0);
    *sz_out=sz; return b;
}

int main(void){
    /* SHA-256 abc */
    mm_sha256_ctx_t hc; uint8_t h[32]; char hex[65];
    mm_sha256_init(&hc); mm_sha256_update(&hc,(const uint8_t*)"abc",3); mm_sha256_final(&hc,h); mm_sha256_hex(h,hex);
    assert(strcmp(hex,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")==0);

    size_t sz; unsigned char *elf=build_raw_elf(&sz);
    FILE *tmp=tmpfile(); assert(tmp); mm_jsonl_t out={tmp}; mm_stats_t stats={0}; char err[256];
    int rc=mm_parse_image("/test/libTest.sprx",elf,sz,"deadbeef",&out,&stats,err,sizeof(err));
    assert(rc==0); assert(stats.images_parsed==1); assert(stats.raw_elf==1); assert(stats.symbols==1); assert(stats.imports==1); assert(stats.relocations==1); assert(stats.dependencies==1);
    fflush(tmp); fseek(tmp,0,SEEK_SET); char all[32768]; size_t n=fread(all,1,sizeof(all)-1,tmp); all[n]=0;
    assert(strstr(all,"g2tViFIohHE")); assert(strstr(all,"libTest.sprx")); assert(strstr(all,"\"classification\":\"import\""));
    fclose(tmp); free(elf);
    puts("HOST_PARSER_TEST=PASS");
    return 0;
}

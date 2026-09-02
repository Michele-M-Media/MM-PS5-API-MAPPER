#include "mm_runtime.h"
#include "mm_elf.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

const intptr_t KERNEL_ADDRESS_TEXT_BASE=(intptr_t)0xffff800000000000ULL;
const intptr_t KERNEL_ADDRESS_ALLPROC=(intptr_t)0xffff800000001000ULL;
const intptr_t KERNEL_ADDRESS_ROOTVNODE=(intptr_t)0xffff800000010000ULL;
const intptr_t KERNEL_ADDRESS_BUS_DATA_DEVICES=(intptr_t)0xffff800000011000ULL;
const size_t KERNEL_OFFSET_PROC_P_PID=0xbc;
const size_t KERNEL_OFFSET_PROC_P_FD=0x48;
const size_t KERNEL_OFFSET_PROC_P_VMSPACE=0x200;
const size_t KERNEL_OFFSET_FILEDESC_FD_RDIR=0x10;
const size_t KERNEL_OFFSET_FILEDESC_FD_JDIR=0x18;
unsigned long KERNEL_OFFSET_VMSPACE_P_ROOT=0x1d0;
uint32_t kernel_get_fw_version(void){return 0x00000000u;}

typedef struct block { uint64_t base; unsigned char *data; size_t size; } block_t;
static block_t blocks[64]; static size_t nblocks;
static void add_block(uint64_t base, void *data, size_t size){blocks[nblocks++]=(block_t){base,(unsigned char*)data,size};}
int32_t kernel_copyout(intptr_t kaddr, void *uaddr, size_t len){
    uint64_t a=(uint64_t)kaddr;
    for(size_t i=0;i<nblocks;i++){
        if(a>=blocks[i].base && a-blocks[i].base<=blocks[i].size && len<=blocks[i].size-(size_t)(a-blocks[i].base)){
            memcpy(uaddr,blocks[i].data+(size_t)(a-blocks[i].base),len);return 0;
        }
    }
    return -1;
}

int main(void){
    const uint64_t PROC=0xffff800000002000ULL, HEAD=0xffff800000003000ULL, OBJ=0xffff800000004000ULL;
    const uint64_t PATH=0xffff800000005000ULL, DYNSEC=0xffff800000006000ULL, STR=0xffff800000007000ULL;
    const uint64_t SYM=0xffff800000008000ULL, DYN=0xffff800000009000ULL;
    const uint64_t THREAD=0xffff80000000a000ULL, FILEDESC=0xffff80000000b000ULL, FDFILES=0xffff80000000c000ULL;
    const uint64_t VMSPACE=0xffff80000000d000ULL, VMENTRY=0xffff80000000e000ULL;
    const uint64_t ROOTVNODE=0xffff800000012000ULL, JAILVNODE=0xffff800000013000ULL, BUSFIRST=0xffff800000014000ULL;
    const uint64_t FDEFILE=0xffff800000015000ULL, FILEDATA=0xffff800000016000ULL;
    const uint64_t BUSNAME=0xffff800000017000ULL, BUSDESC=0xffff800000018000ULL, BUSDRIVER=0xffff800000019000ULL;
    const uint64_t DRIVERNAME=0xffff80000001a000ULL, METHODS=0xffff80000001b000ULL, METHODDESC=0xffff80000001c000ULL, SOFTC=0xffff80000001d000ULL;
    const uint64_t BUSRAW=0xffff80000001e000ULL, BUSLAST=0xffff80000001f000ULL;
    uint64_t allproc=PROC; add_block((uint64_t)KERNEL_ADDRESS_ALLPROC,&allproc,sizeof(allproc));
    unsigned char proc[0x500]; memset(proc,0,sizeof(proc));
    *(uint64_t*)(proc+0)=0; *(uint64_t*)(proc+0x10)=THREAD; *(uint64_t*)(proc+KERNEL_OFFSET_PROC_P_FD)=FILEDESC;
    *(uint32_t*)(proc+KERNEL_OFFSET_PROC_P_PID)=123; *(uint64_t*)(proc+KERNEL_OFFSET_PROC_P_VMSPACE)=VMSPACE; *(uint64_t*)(proc+0x3e8)=HEAD; add_block(PROC,proc,sizeof(proc));

    unsigned char thr[0x120]; memset(thr,0,sizeof(thr)); *(uint64_t*)(thr+0x10)=0; *(uint64_t*)(thr+0x9c)=456; add_block(THREAD,thr,sizeof(thr));
    unsigned char fd[0x40]; memset(fd,0,sizeof(fd)); *(uint64_t*)(fd+0)=FDFILES; *(uint64_t*)(fd+KERNEL_OFFSET_FILEDESC_FD_RDIR)=ROOTVNODE; *(uint64_t*)(fd+KERNEL_OFFSET_FILEDESC_FD_JDIR)=JAILVNODE; add_block(FILEDESC,fd,sizeof(fd));
    unsigned char fdt[8+0x30*3]; memset(fdt,0,sizeof(fdt)); *(int32_t*)(fdt+0)=3; *(uint64_t*)(fdt+8+0x30)=FDEFILE; add_block(FDFILES,fdt,sizeof(fdt));
    uint64_t file_data=FILEDATA; add_block(FDEFILE,&file_data,sizeof(file_data));
    unsigned char filedata_raw[256]; memset(filedata_raw,0,sizeof(filedata_raw)); *(uint64_t*)(filedata_raw+0)=BUSFIRST; *(uint64_t*)(filedata_raw+8)=BUSDRIVER; *(uint64_t*)(filedata_raw+16)=SOFTC; add_block(FILEDATA,filedata_raw,sizeof(filedata_raw));
    unsigned char vmspace[0x220]; memset(vmspace,0,sizeof(vmspace)); *(uint64_t*)(vmspace+KERNEL_OFFSET_VMSPACE_P_ROOT)=VMENTRY; add_block(VMSPACE,vmspace,sizeof(vmspace));
    unsigned char vme[0x80]; memset(vme,0,sizeof(vme)); *(uint64_t*)(vme+0x08)=VMSPACE; *(uint64_t*)(vme+0x10)=0; *(uint64_t*)(vme+0x18)=0; *(uint64_t*)(vme+0x20)=0x400000; *(uint64_t*)(vme+0x28)=0x410000; *(uint8_t*)(vme+0x64)=5; add_block(VMENTRY,vme,sizeof(vme));
    uint64_t rootvnode_value=ROOTVNODE; add_block((uint64_t)KERNEL_ADDRESS_ROOTVNODE,&rootvnode_value,sizeof(rootvnode_value));
    uint64_t busraw[2]={BUSFIRST,BUSLAST+0x18}; add_block((uint64_t)KERNEL_ADDRESS_BUS_DATA_DEVICES,busraw,sizeof(busraw));
    const char busname[]="testbus0"; add_block(BUSNAME,(void*)busname,sizeof(busname));
    const char busdesc[]="Synthetic test bus"; add_block(BUSDESC,(void*)busdesc,sizeof(busdesc));
    const char drivername[]="testbus"; add_block(DRIVERNAME,(void*)drivername,sizeof(drivername));
    unsigned char busdev[0x90]; memset(busdev,0,sizeof(busdev)); *(uint64_t*)(busdev+0x18)=BUSRAW; *(uint64_t*)(busdev+0x40)=BUSDRIVER; *(uint32_t*)(busdev+0x50)=0; *(uint64_t*)(busdev+0x58)=BUSNAME; *(uint64_t*)(busdev+0x60)=BUSDESC; *(uint32_t*)(busdev+0x6c)=2; *(uint64_t*)(busdev+0x88)=SOFTC; add_block(BUSFIRST,busdev,sizeof(busdev));
    unsigned char drv[0x30]; memset(drv,0,sizeof(drv)); *(uint64_t*)(drv+0)=DRIVERNAME; *(uint64_t*)(drv+8)=METHODS; *(uint64_t*)(drv+0x10)=0x100; *(uint32_t*)(drv+0x20)=1; add_block(BUSDRIVER,drv,sizeof(drv));
    uint64_t methods[4]={METHODDESC,(uint64_t)KERNEL_ADDRESS_TEXT_BASE+0x1234,0,0}; add_block(METHODS,methods,sizeof(methods));
    unsigned char mdesc[24]; memset(mdesc,0,sizeof(mdesc)); *(uint32_t*)(mdesc+0)=77; *(uint64_t*)(mdesc+16)=(uint64_t)KERNEL_ADDRESS_TEXT_BASE+0x2222; add_block(METHODDESC,mdesc,sizeof(mdesc));
    unsigned char busrawnode[0x90]; memset(busrawnode,0,sizeof(busrawnode)); *(uint64_t*)(busrawnode+0x18)=BUSLAST; *(uint64_t*)(busrawnode+0x58)=0; add_block(BUSRAW,busrawnode,sizeof(busrawnode));
    unsigned char buslast[0x90]; memset(buslast,0,sizeof(buslast)); *(uint64_t*)(buslast+0x18)=0; *(uint64_t*)(buslast+0x40)=BUSDRIVER; *(uint32_t*)(buslast+0x50)=1; *(uint64_t*)(buslast+0x58)=BUSNAME; *(uint64_t*)(buslast+0x60)=BUSDESC; *(uint32_t*)(buslast+0x6c)=2; *(uint64_t*)(buslast+0x88)=SOFTC; add_block(BUSLAST,buslast,sizeof(buslast));
    unsigned char softc[256]; for(size_t si=0;si<sizeof(softc);si++) softc[si]=(unsigned char)si; add_block(SOFTC,softc,sizeof(softc));
    uint64_t first=OBJ; add_block(HEAD,&first,sizeof(first));
    const char path[]="/system/common/lib/libSceTest.sprx"; add_block(PATH,(void*)path,sizeof(path));
    const unsigned char strtab[]="\0AbCdEfGh12+#X#Y\0libSceTest.sprx\0"; add_block(STR,(void*)strtab,sizeof(strtab));
    mm_elf64_sym_t syms[2]; memset(syms,0,sizeof(syms)); syms[1].st_name=1; syms[1].st_info=(1u<<4)|2u; syms[1].st_shndx=1; syms[1].st_value=0x1234; syms[1].st_size=32; add_block(SYM,syms,sizeof(syms));
    mm_elf64_dyn_t dyn[2]; memset(dyn,0,sizeof(dyn)); dyn[0].d_tag=MM_DT_SONAME; dyn[0].d_val=18; dyn[1].d_tag=MM_DT_NULL; add_block(DYN,dyn,sizeof(dyn));
    mm_dynlib_dynsec_t ds; memset(&ds,0,sizeof(ds)); ds.symtab=SYM; ds.symtabsize=sizeof(syms); ds.strtab=STR; ds.strtabsize=sizeof(strtab); ds.dynamic=DYN; ds.dynamicsize=sizeof(dyn); add_block(DYNSEC,&ds,sizeof(ds));
    mm_dynlib_obj_t obj; memset(&obj,0,sizeof(obj)); obj.next=0;obj.path=PATH;obj.handle=0x42;obj.mapbase=0x800000000ULL;obj.mapsize=0x10000;obj.textsize=0x8000;obj.entry=0x800001000ULL;obj.dynsec=DYNSEC; add_block(OBJ,&obj,sizeof(obj));

    FILE *tmp=tmpfile(); if(!tmp)return 2; mm_jsonl_t out={tmp}; mm_stats_t st; memset(&st,0,sizeof(st));
    if(mm_runtime_scan_all_processes(&out,&st)!=0)return 3;
    if(st.runtime_processes!=1 || st.runtime_modules!=1 || st.runtime_dynsecs!=1 || st.runtime_symbols!=1 || st.runtime_exports!=1)return 4;
    if(st.runtime_process_resources!=1 || st.runtime_threads!=1 || st.runtime_vmspaces!=1 || st.runtime_vm_regions!=1 || st.runtime_system_anchors!=3)return 9;
    if(st.runtime_fd_tables!=1 || st.runtime_fds!=1 || st.runtime_fd_data_raw!=1 || st.runtime_fd_data_raw_bytes!=256 || st.runtime_fd_data_pointer_candidates!=3 || st.runtime_bus_devices!=2 || st.runtime_bus_nodes_walked!=3 || st.runtime_bus_raw_candidates!=1 || st.runtime_bus_list_terminators!=1 || st.runtime_bus_snapshot_stable!=1 || st.runtime_bus_drivers!=1 || st.runtime_bus_driver_methods!=1 || st.runtime_bus_softc_heads!=2)return 11;
    if(st.runtime_errors!=0 || st.runtime_observable_limits!=0)return 10;
    fflush(tmp); fseek(tmp,0,SEEK_END); long sz=ftell(tmp); if(sz<=0)return 5; fseek(tmp,0,SEEK_SET);
    char *buf=calloc(1,(size_t)sz+1); if(!buf)return 6;
    size_t got=fread(buf,1,(size_t)sz,tmp);
    if(got!=(size_t)sz){free(buf); fclose(tmp); return 7;}
    if(!strstr(buf,"runtime_module") || !strstr(buf,"runtime_symbol") || !strstr(buf,"libSceTest.sprx") ||
       !strstr(buf,"runtime_process_resources") || !strstr(buf,"runtime_thread") || !strstr(buf,"runtime_vm_region") || !strstr(buf,"runtime_vm_terminator") || !strstr(buf,"runtime_system_anchor") ||
       !strstr(buf,"runtime_fd_table") || !strstr(buf,"runtime_fd") || !strstr(buf,"runtime_fd_data_raw") || !strstr(buf,"runtime_fd_data_pointer_candidate") || !strstr(buf,"runtime_bus_list_head") || !strstr(buf,"runtime_bus_device") || !strstr(buf,"runtime_bus_device_raw_candidate") || !strstr(buf,"runtime_bus_list_terminator") || !strstr(buf,"runtime_bus_list_consistency") || !strstr(buf,"runtime_bus_driver_method") || !strstr(buf,"runtime_bus_softc_head")){free(buf); fclose(tmp); return 8;}
    free(buf); fclose(tmp); puts("HOST_RUNTIME_CENSUS_TEST=PASS"); return 0;
}

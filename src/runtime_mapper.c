#define _POSIX_C_SOURCE 200809L
#include "mm_runtime.h"
#include "mm_elf.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__has_include)
# if __has_include(<ps5/kernel.h>)
#  include <ps5/kernel.h>
#  define MM_RUNTIME_HAVE_PS5 1
# endif
#endif
#ifndef MM_RUNTIME_HAVE_PS5
#define MM_RUNTIME_HAVE_PS5 0
#endif

#if MM_RUNTIME_HAVE_PS5
/* Present in the ps5-payload-dev/sdk CRT (crt/kernel.c) but not exposed by
 * every installed public header revision.  Using the CRT-initialized value
 * keeps the VM-tree walk firmware-aware instead of hardcoding 10.xx/11.xx/12.xx. */
extern unsigned long KERNEL_OFFSET_VMSPACE_P_ROOT;
#endif

#define MM_RUNTIME_PROC_DYNLIB_HEAD_OFFSET 0x3e8ULL
/* Source-grounded by ps5-payload-dev/sdk crt/kernel.c::kernel_get_proc_thread. */
#define MM_RUNTIME_PROC_THREAD_HEAD_OFFSET 0x10ULL
#define MM_RUNTIME_THREAD_NEXT_OFFSET 0x10ULL
#define MM_RUNTIME_THREAD_TID_OFFSET 0x9cULL
/* Source-grounded by kernel_get_vmem_entry/kernel_get_vmem_protection. */
#define MM_RUNTIME_VM_ENTRY_NEXT_OFFSET 0x08ULL
#define MM_RUNTIME_VM_ENTRY_LEFT_OFFSET 0x10ULL
#define MM_RUNTIME_VM_ENTRY_START_OFFSET 0x20ULL
#define MM_RUNTIME_VM_ENTRY_END_OFFSET 0x28ULL
#define MM_RUNTIME_VM_ENTRY_PROT_OFFSET 0x64ULL
#define MM_RUNTIME_MAX_PROCESSES 4096ULL
#define MM_RUNTIME_MAX_MODULES_PER_PROCESS 8192ULL
#define MM_RUNTIME_MAX_THREADS_PER_PROCESS 16384ULL
#define MM_RUNTIME_MAX_VM_REGIONS_PER_PROCESS 262144ULL
#define MM_RUNTIME_MAX_VM_TREE_DEPTH 256ULL
#define MM_RUNTIME_MAX_STRTAB_SIZE (64ULL * 1024ULL * 1024ULL)
#define MM_RUNTIME_MAX_SYMBOLS 2000000ULL
#define MM_RUNTIME_MAX_DYNAMIC_ENTRIES 262144ULL
#define MM_RUNTIME_MAX_RELOCS 4000000ULL
#define MM_RUNTIME_MAX_PATH 1024U
#define MM_RUNTIME_RAW_TABLE_CAP (32ULL * 1024ULL * 1024ULL)
#define MM_RUNTIME_RAW_CHUNK 4096ULL
#define MM_RUNTIME_MAX_STRTAB_STRINGS 2000000ULL

/* Source-grounded FD table layout. ps5-payload-dev/sdk
 * kernel_get_proc_file() reads fd_files from filedesc, then fde_file from
 * fd_files + 8 + (0x30 * fd), and finally the first qword of that object. */
#define MM_RUNTIME_FDT_NFILES_OFFSET 0x00ULL
#define MM_RUNTIME_FDT_ENTRIES_OFFSET 0x08ULL
#define MM_RUNTIME_FDE_STRIDE 0x30ULL
#define MM_RUNTIME_FDE_FILE_OFFSET 0x00ULL
#define MM_RUNTIME_MAX_FDS_PER_PROCESS 65536ULL
#define MM_RUNTIME_FD_DATA_RAW_SIZE 0x100ULL

/* Kernel bus-device prefix. The PS5-specific cragson/a53-code-exec
 * public PoC confirms global-next +0x18, nameunit +0x58 and softc +0x88.
 * The remaining prefix offsets align with the FreeBSD _device lineage and
 * are only decoded after pointer/string sanity validation. */
#define MM_BUS_DEV_NEXT 0x18ULL
#define MM_BUS_DEV_PARENT 0x28ULL
#define MM_BUS_DEV_DRIVER 0x40ULL
#define MM_BUS_DEV_DEVCLASS 0x48ULL
#define MM_BUS_DEV_UNIT 0x50ULL
#define MM_BUS_DEV_NAMEUNIT 0x58ULL
#define MM_BUS_DEV_DESC 0x60ULL
#define MM_BUS_DEV_BUSY 0x68ULL
#define MM_BUS_DEV_STATE 0x6cULL
#define MM_BUS_DEV_DEVFLAGS 0x70ULL
#define MM_BUS_DEV_FLAGS 0x74ULL
#define MM_BUS_DEV_ORDER 0x78ULL
#define MM_BUS_DEV_IVARS 0x80ULL
#define MM_BUS_DEV_SOFTC 0x88ULL
#define MM_BUS_DEV_PREFIX_SIZE 0x90ULL
#define MM_RUNTIME_MAX_BUS_DEVICES 16384ULL
#define MM_RUNTIME_MAX_DRIVER_METHODS 2048ULL
#define MM_RUNTIME_BUS_SOFTC_HEAD 256ULL

_Static_assert(sizeof(mm_dynlib_dynsec_t) == 0x120, "dynlib_dynsec layout mismatch");
_Static_assert(offsetof(mm_dynlib_dynsec_t, symtab) == 0x28, "dynsec.symtab offset mismatch");
_Static_assert(offsetof(mm_dynlib_dynsec_t, strtab) == 0x38, "dynsec.strtab offset mismatch");
_Static_assert(offsetof(mm_dynlib_dynsec_t, dynamic) == 0x78, "dynsec.dynamic offset mismatch");
_Static_assert(sizeof(mm_dynlib_obj_t) == 0x180, "dynlib_obj layout mismatch");
_Static_assert(offsetof(mm_dynlib_obj_t, handle) == 0x28, "dynlib.handle offset mismatch");
_Static_assert(offsetof(mm_dynlib_obj_t, mapbase) == 0x30, "dynlib.mapbase offset mismatch");
_Static_assert(offsetof(mm_dynlib_obj_t, dynsec) == 0x148, "dynlib.dynsec offset mismatch");

#if MM_RUNTIME_HAVE_PS5
static size_t mm_bounded_strlen(const char *s, size_t cap) {
    size_t n=0;
    if(!s) return 0;
    while(n<cap && s[n]) n++;
    return n;
}

static void mm_json_hex(FILE *f, const unsigned char *p, size_t n) {
    static const char h[]="0123456789abcdef";
    fputc('\"',f);
    for(size_t i=0;i<n;i++) { fputc(h[p[i]>>4],f); fputc(h[p[i]&15],f); }
    fputc('\"',f);
}

static int mm_obj_extended_layout_sane(const mm_dynlib_obj_t *o) {
    if(!o || !o->mapbase || !o->mapsize) return 0;
    if(o->textsize > o->mapsize || o->datasize > o->mapsize) return 0;
    if(o->entry && (o->entry < o->mapbase || o->entry > o->mapbase + o->mapsize + 0x100000ULL)) return 0;
    return 1;
}

static int mm_is_kernel_ptr(uint64_t p) {
    if(!p) return 0;
    return (p >> 48) == 0xffffULL;
}

static int mm_nid_prefix(const char *s, size_t len, char out[12]) {
    if(!s || len < 11) return 0;
    for(size_t i=0;i<11;i++) {
        unsigned char c=(unsigned char)s[i];
        int ok=(c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='+'||c=='-';
        if(!ok) return 0;
    }
    if(len>11 && s[11] != '#') return 0;
    memcpy(out,s,11); out[11]=0; return 1;
}

static const char *mm_dyn_tag_name(int64_t t) {
    switch(t) {
        case MM_DT_NULL:return "DT_NULL"; case MM_DT_NEEDED:return "DT_NEEDED";
        case MM_DT_PLTRELSZ:return "DT_PLTRELSZ"; case MM_DT_PLTGOT:return "DT_PLTGOT";
        case MM_DT_HASH:return "DT_HASH"; case MM_DT_STRTAB:return "DT_STRTAB";
        case MM_DT_SYMTAB:return "DT_SYMTAB"; case MM_DT_RELA:return "DT_RELA";
        case MM_DT_RELASZ:return "DT_RELASZ"; case MM_DT_RELAENT:return "DT_RELAENT";
        case MM_DT_STRSZ:return "DT_STRSZ"; case MM_DT_SYMENT:return "DT_SYMENT";
        case MM_DT_INIT:return "DT_INIT"; case MM_DT_FINI:return "DT_FINI";
        case MM_DT_SONAME:return "DT_SONAME"; case MM_DT_RPATH:return "DT_RPATH";
        case MM_DT_SYMBOLIC:return "DT_SYMBOLIC"; case MM_DT_REL:return "DT_REL";
        case MM_DT_RELSZ:return "DT_RELSZ"; case MM_DT_RELENT:return "DT_RELENT";
        case MM_DT_PLTREL:return "DT_PLTREL"; case MM_DT_DEBUG:return "DT_DEBUG";
        case MM_DT_TEXTREL:return "DT_TEXTREL"; case MM_DT_JMPREL:return "DT_JMPREL";
        case MM_DT_BIND_NOW:return "DT_BIND_NOW"; case MM_DT_INIT_ARRAY:return "DT_INIT_ARRAY";
        case MM_DT_FINI_ARRAY:return "DT_FINI_ARRAY"; case MM_DT_INIT_ARRAYSZ:return "DT_INIT_ARRAYSZ";
        case MM_DT_FINI_ARRAYSZ:return "DT_FINI_ARRAYSZ"; case MM_DT_RUNPATH:return "DT_RUNPATH";
        case MM_DT_FLAGS:return "DT_FLAGS"; case MM_DT_PREINIT_ARRAY:return "DT_PREINIT_ARRAY";
        case MM_DT_PREINIT_ARRAYSZ:return "DT_PREINIT_ARRAYSZ"; case MM_DT_RELACOUNT:return "DT_RELACOUNT";
        case MM_DT_SCE_FINGERPRINT:return "DT_SCE_FINGERPRINT"; case MM_DT_SCE_ORIGFILENAME:return "DT_SCE_ORIGFILENAME";
        case MM_DT_SCE_MODULEINFO:return "DT_SCE_MODULEINFO"; case MM_DT_SCE_NEEDED_MODULE:return "DT_SCE_NEEDED_MODULE";
        case MM_DT_SCE_MODULE_ATTR:return "DT_SCE_MODULE_ATTR"; case MM_DT_SCE_EXPLIB:return "DT_SCE_EXPLIB";
        case MM_DT_SCE_IMPLIB:return "DT_SCE_IMPLIB"; case MM_DT_SCE_EXPORT_LIB_ATTR:return "DT_SCE_EXPORT_LIB_ATTR";
        case MM_DT_SCE_IMPORT_LIB_ATTR:return "DT_SCE_IMPORT_LIB_ATTR"; case MM_DT_SCE_HASH:return "DT_SCE_HASH";
        case MM_DT_SCE_PLTGOT:return "DT_SCE_PLTGOT"; case MM_DT_SCE_JMPREL:return "DT_SCE_JMPREL";
        case MM_DT_SCE_PLTREL:return "DT_SCE_PLTREL";
        case MM_DT_SCE_PLTRELSZ:return "DT_SCE_PLTRELSZ"; case MM_DT_SCE_RELA:return "DT_SCE_RELA";
        case MM_DT_SCE_RELASZ:return "DT_SCE_RELASZ"; case MM_DT_SCE_RELAENT:return "DT_SCE_RELAENT";
        case MM_DT_SCE_STRTAB:return "DT_SCE_STRTAB"; case MM_DT_SCE_STRSIZE:return "DT_SCE_STRSIZE";
        case MM_DT_SCE_SYMTAB:return "DT_SCE_SYMTAB"; case MM_DT_SCE_SYMENT:return "DT_SCE_SYMENT";
        case MM_DT_SCE_HASHSZ:return "DT_SCE_HASHSZ"; case MM_DT_SCE_SYMTABSZ:return "DT_SCE_SYMTABSZ";
        case MM_DT_SCE_PS5_ORIGFILENAME:return "DT_SCE_PS5_ORIGFILENAME";
        case MM_DT_SCE_PS5_MODULEINFO:return "DT_SCE_PS5_MODULEINFO";
        case MM_DT_SCE_PS5_IMPORT_MODULE:return "DT_SCE_PS5_IMPORT_MODULE";
        case MM_DT_SCE_PS5_EXPORT_LIB:return "DT_SCE_PS5_EXPORT_LIB";
        case MM_DT_SCE_PS5_IMPORT_LIB:return "DT_SCE_PS5_IMPORT_LIB"; default:return "UNKNOWN";
    }
}

static int mm_kread(uint64_t addr, void *buf, size_t len) {
    if(!addr || !buf || !len) return -1;
    return kernel_copyout((intptr_t)addr,buf,len) < 0 ? -1 : 0;
}

static int mm_kread_cstr(uint64_t addr, char out[MM_RUNTIME_MAX_PATH]) {
    if(!mm_is_kernel_ptr(addr)) return -1;
    memset(out,0,MM_RUNTIME_MAX_PATH);
    size_t off=0;
    while(off<MM_RUNTIME_MAX_PATH-1) {
        unsigned char chunk[32];
        size_t want=(MM_RUNTIME_MAX_PATH-1-off)<sizeof(chunk)?(MM_RUNTIME_MAX_PATH-1-off):sizeof(chunk);
        if(mm_kread(addr+off,chunk,want)==0) {
            for(size_t i=0;i<want;i++) {
                out[off+i]=(char)chunk[i];
                if(chunk[i]==0) return 0;
            }
            off+=want;
            continue;
        }
        /* Mapping-edge fallback: copy one byte at a time until NUL or fault. */
        for(size_t i=0;i<want;i++) {
            unsigned char c=0;
            if(mm_kread(addr+off+i,&c,1)<0) return off?0:-1;
            out[off+i]=(char)c;
            if(c==0) return 0;
        }
        off+=want;
    }
    out[MM_RUNTIME_MAX_PATH-1]=0;
    return 0;
}

static void mm_emit_runtime_gap(mm_jsonl_t *out, mm_stats_t *stats, uint32_t pid,
                                const char *path, const char *stage, const char *reason,
                                uint64_t address, uint64_t size) {
    FILE *f=out->fp;
    fputs("{\"record\":\"runtime_coverage_gap\",\"pid\":",f); fprintf(f,"%u",pid);
    fputs(",\"path\":",f); mm_json_cstr(f,path?path:"");
    fputs(",\"stage\":",f); mm_json_cstr(f,stage?stage:"");
    fputs(",\"reason\":",f); mm_json_cstr(f,reason?reason:"");
    fprintf(f,",\"address\":\"0x%016llx\",\"size\":%llu}\n",
            (unsigned long long)address,(unsigned long long)size);
    stats->runtime_errors++;
}

static void mm_emit_runtime_limit(mm_jsonl_t *out, mm_stats_t *stats, const char *stage,
                                  const char *reason, uint64_t address, uint64_t size) {
    FILE *f=out->fp;
    fputs("{\"record\":\"runtime_observable_limit\",\"stage\":",f);
    mm_json_cstr(f,stage?stage:"");
    fputs(",\"reason\":",f); mm_json_cstr(f,reason?reason:"");
    fprintf(f,",\"address\":\"0x%016llx\",\"size\":%llu}\n",
            (unsigned long long)address,(unsigned long long)size);
    stats->runtime_observable_limits++;
}

static int mm_runtime_string(const unsigned char *strtab, uint64_t strsz, uint64_t off,
                             const unsigned char **ptr, size_t *len) {
    if(!strtab || off>=strsz) return -1;
    *ptr=strtab+off;
    *len=0;
    while(off+*len<strsz && (*ptr)[*len]) (*len)++;
    return 0;
}

static void mm_scan_runtime_strtab_strings(mm_jsonl_t *out, mm_stats_t *stats, uint32_t pid,
                                            const char *path, const unsigned char *strtab, uint64_t strsz) {
    if(!strtab || !strsz) return;
    uint64_t off=0,count=0;
    while(off<strsz && count<MM_RUNTIME_MAX_STRTAB_STRINGS) {
        uint64_t start=off;
        while(off<strsz && strtab[off]) off++;
        uint64_t len=off-start;
        if(len) {
            FILE *f=out->fp;
            fputs("{\"record\":\"runtime_strtab_string\",\"pid\":",f); fprintf(f,"%u",pid);
            fputs(",\"path\":",f); mm_json_cstr(f,path);
            fprintf(f,",\"offset\":%llu,\"value\":",(unsigned long long)start);
            mm_json_string(f,strtab+start,(size_t)len); fputs("}\n",f);
            stats->runtime_strtab_strings++; count++;
        }
        off++;
    }
    if(count>=MM_RUNTIME_MAX_STRTAB_STRINGS && off<strsz)
        mm_emit_runtime_gap(out,stats,pid,path,"strtab_strings","string inventory reached sanity cap",0,strsz);
}

static void mm_emit_runtime_raw_table(mm_jsonl_t *out, mm_stats_t *stats, uint32_t pid,
                                      const char *path, const char *kind, uint64_t addr, uint64_t size) {
    if(!addr || !size) return;
    if(!mm_is_kernel_ptr(addr)) {
        mm_emit_runtime_gap(out,stats,pid,path,kind,"raw table pointer was not a kernel pointer",addr,size);
        return;
    }
    uint64_t total=size;
    if(total>MM_RUNTIME_RAW_TABLE_CAP) {
        mm_emit_runtime_gap(out,stats,pid,path,kind,"raw table truncated at capture sanity cap",addr,size);
        total=MM_RUNTIME_RAW_TABLE_CAP;
    }
    unsigned char *buf=(unsigned char*)malloc((size_t)MM_RUNTIME_RAW_CHUNK);
    if(!buf) { mm_emit_runtime_gap(out,stats,pid,path,kind,"malloc failed",addr,total); return; }
    uint64_t chunks=0;
    for(uint64_t off=0; off<total; off+=MM_RUNTIME_RAW_CHUNK) {
        uint64_t n=total-off; if(n>MM_RUNTIME_RAW_CHUNK)n=MM_RUNTIME_RAW_CHUNK;
        if(mm_kread(addr+off,buf,(size_t)n)<0) {
            mm_emit_runtime_gap(out,stats,pid,path,kind,"kernel_copyout failed while capturing raw loader table",addr+off,n);
            break;
        }
        FILE *f=out->fp;
        fputs("{\"record\":\"runtime_raw_table_chunk\",\"pid\":",f); fprintf(f,"%u",pid);
        fputs(",\"path\":",f); mm_json_cstr(f,path); fputs(",\"kind\":",f); mm_json_cstr(f,kind);
        fprintf(f,",\"base\":\"0x%016llx\",\"total_size\":%llu,\"offset\":%llu,\"size\":%llu,\"hex\":",
                (unsigned long long)addr,(unsigned long long)size,(unsigned long long)off,(unsigned long long)n);
        mm_json_hex(f,buf,(size_t)n); fputs("}\n",f);
        stats->runtime_raw_chunks++; stats->runtime_raw_bytes+=n; chunks++;
    }
    if(chunks) stats->runtime_raw_blobs++;
    free(buf);
}

static void mm_emit_runtime_library_attr(mm_jsonl_t *out, mm_stats_t *stats, uint32_t pid,
                                         const char *path, const char *kind, uint64_t raw) {
    FILE *f=out->fp;
    uint16_t id=(uint16_t)(raw>>48);
    uint64_t attrs=raw & 0x0000ffffffffffffULL;
    fputs("{\"record\":\"runtime_library_attr\",\"pid\":",f); fprintf(f,"%u",pid);
    fputs(",\"path\":",f); mm_json_cstr(f,path); fputs(",\"kind\":",f); mm_json_cstr(f,kind);
    fprintf(f,",\"object_id\":%u,\"attrs\":\"0x%012llx\",\"raw\":\"0x%016llx\"}\n",
            (unsigned)id,(unsigned long long)attrs,(unsigned long long)raw);
    stats->runtime_library_attrs++;
}

static void mm_emit_runtime_object_ref(mm_jsonl_t *out,uint32_t pid,const char *path,
                                       const char *kind,uint64_t value,
                                       const unsigned char *name,size_t name_len) {
    FILE *f=out->fp;
    uint16_t id=(uint16_t)(value>>48);
    uint8_t major=(uint8_t)(value>>32);
    uint8_t minor=(uint8_t)(value>>40);
    fputs("{\"record\":\"runtime_library_ref\",\"pid\":",f); fprintf(f,"%u",pid);
    fputs(",\"path\":",f); mm_json_cstr(f,path);
    fputs(",\"kind\":",f); mm_json_cstr(f,kind);
    fprintf(f,",\"object_id\":%u,\"version_major\":%u,\"version_minor\":%u,\"name\":",
            (unsigned)id,(unsigned)major,(unsigned)minor);
    mm_json_string(f,name,name_len);
    fprintf(f,",\"raw\":\"0x%016llx\"}\n",(unsigned long long)value);
}

static void mm_scan_runtime_dynamic(mm_jsonl_t *out, mm_stats_t *stats, uint32_t pid,
                                    const char *path, const mm_dynlib_dynsec_t *ds,
                                    const unsigned char *strtab) {
    if(!ds->dynamic || ds->dynamicsize < sizeof(mm_elf64_dyn_t)) return;
    uint64_t count=ds->dynamicsize/sizeof(mm_elf64_dyn_t);
    if(count>MM_RUNTIME_MAX_DYNAMIC_ENTRIES) {
        mm_emit_runtime_gap(out,stats,pid,path,"dynamic","entry count exceeded sanity cap",ds->dynamic,ds->dynamicsize);
        count=MM_RUNTIME_MAX_DYNAMIC_ENTRIES;
    }
    for(uint64_t i=0;i<count;i++) {
        mm_elf64_dyn_t d;
        uint64_t addr=ds->dynamic+i*sizeof(d);
        if(mm_kread(addr,&d,sizeof(d))<0) {
            mm_emit_runtime_gap(out,stats,pid,path,"dynamic","kernel_copyout failed",addr,sizeof(d));
            break;
        }
        if(d.d_tag==MM_DT_NULL) break;
        FILE *f=out->fp;
        fputs("{\"record\":\"runtime_dynamic\",\"pid\":",f); fprintf(f,"%u",pid);
        fputs(",\"path\":",f); mm_json_cstr(f,path);
        fprintf(f,",\"index\":%llu,\"tag\":\"0x%016llx\",\"tag_name\":",
                (unsigned long long)i,(unsigned long long)d.d_tag);
        mm_json_cstr(f,mm_dyn_tag_name(d.d_tag));
        fprintf(f,",\"value\":\"0x%016llx\"}\n",(unsigned long long)d.d_val);
        stats->runtime_dynamic_entries++;

        if(!strtab || !ds->strtabsize) continue;
        const unsigned char *sp=0; size_t sl=0;
        if(d.d_tag==MM_DT_NEEDED || d.d_tag==MM_DT_SONAME || d.d_tag==MM_DT_RPATH || d.d_tag==MM_DT_RUNPATH ||
           d.d_tag==MM_DT_SCE_ORIGFILENAME || d.d_tag==MM_DT_SCE_PS5_ORIGFILENAME) {
            if(mm_runtime_string(strtab,ds->strtabsize,d.d_val,&sp,&sl)==0) {
                const char *kind=d.d_tag==MM_DT_NEEDED?"needed_file":(d.d_tag==MM_DT_SONAME?"soname":(d.d_tag==MM_DT_RPATH?"rpath":(d.d_tag==MM_DT_RUNPATH?"runpath":"orig_filename")));
                fputs("{\"record\":\"runtime_string_ref\",\"pid\":",f); fprintf(f,"%u",pid);
                fputs(",\"path\":",f); mm_json_cstr(f,path); fputs(",\"kind\":",f); mm_json_cstr(f,kind);
                fputs(",\"name\":",f); mm_json_string(f,sp,sl); fputs("}\n",f);
                if(d.d_tag==MM_DT_NEEDED) stats->runtime_dependencies++;
            }
        } else if(d.d_tag==MM_DT_SCE_NEEDED_MODULE || d.d_tag==MM_DT_SCE_PS5_IMPORT_MODULE ||
                  d.d_tag==MM_DT_SCE_IMPLIB || d.d_tag==MM_DT_SCE_PS5_IMPORT_LIB ||
                  d.d_tag==MM_DT_SCE_EXPLIB || d.d_tag==MM_DT_SCE_PS5_EXPORT_LIB ||
                  d.d_tag==MM_DT_SCE_MODULEINFO || d.d_tag==MM_DT_SCE_PS5_MODULEINFO) {
            uint64_t noff=(uint32_t)d.d_val;
            if(mm_runtime_string(strtab,ds->strtabsize,noff,&sp,&sl)==0) {
                const char *kind="object_ref";
                if(d.d_tag==MM_DT_SCE_NEEDED_MODULE || d.d_tag==MM_DT_SCE_PS5_IMPORT_MODULE) kind="import_module";
                else if(d.d_tag==MM_DT_SCE_IMPLIB || d.d_tag==MM_DT_SCE_PS5_IMPORT_LIB) kind="import_library";
                else if(d.d_tag==MM_DT_SCE_EXPLIB || d.d_tag==MM_DT_SCE_PS5_EXPORT_LIB) kind="export_library";
                else if(d.d_tag==MM_DT_SCE_MODULEINFO || d.d_tag==MM_DT_SCE_PS5_MODULEINFO) kind="module_info";
                mm_emit_runtime_object_ref(out,pid,path,kind,d.d_val,sp,sl);
                if(d.d_tag==MM_DT_SCE_NEEDED_MODULE || d.d_tag==MM_DT_SCE_PS5_IMPORT_MODULE) stats->runtime_dependencies++;
            }
        } else if(d.d_tag==MM_DT_SCE_EXPORT_LIB_ATTR || d.d_tag==MM_DT_SCE_IMPORT_LIB_ATTR) {
            mm_emit_runtime_library_attr(out,stats,pid,path,
                    d.d_tag==MM_DT_SCE_EXPORT_LIB_ATTR?"export_library_attr":"import_library_attr",d.d_val);
        }
    }
}

static void mm_scan_runtime_symbols(mm_jsonl_t *out, mm_stats_t *stats, uint32_t pid,
                                    const char *path, const mm_dynlib_obj_t *obj,
                                    const mm_dynlib_dynsec_t *ds,
                                    const unsigned char *strtab) {
    if(!ds->symtab || ds->symtabsize < sizeof(mm_elf64_sym_t) || !strtab || !ds->strtabsize) return;
    uint64_t count=ds->symtabsize/sizeof(mm_elf64_sym_t);
    if(count>MM_RUNTIME_MAX_SYMBOLS) {
        mm_emit_runtime_gap(out,stats,pid,path,"symtab","symbol count exceeded sanity cap",ds->symtab,ds->symtabsize);
        count=MM_RUNTIME_MAX_SYMBOLS;
    }
    for(uint64_t i=1;i<count;i++) {
        mm_elf64_sym_t s;
        uint64_t addr=ds->symtab+i*sizeof(s);
        if(mm_kread(addr,&s,sizeof(s))<0) {
            mm_emit_runtime_gap(out,stats,pid,path,"symtab","kernel_copyout failed",addr,sizeof(s));
            break;
        }
        if(s.st_name>=ds->strtabsize) continue;
        const unsigned char *sp=0; size_t sl=0;
        if(mm_runtime_string(strtab,ds->strtabsize,s.st_name,&sp,&sl)<0 || !sl) continue;
        int is_import=(s.st_shndx==MM_SHN_UNDEF);
        int is_export=!is_import && (mm_elf_st_bind(s.st_info)==1 || mm_elf_st_bind(s.st_info)==2);
        char nid[12]={0}; int has_nid=mm_nid_prefix((const char*)sp,sl,nid);
        FILE *f=out->fp;
        fputs("{\"record\":\"runtime_symbol\",\"pid\":",f); fprintf(f,"%u",pid);
        fputs(",\"path\":",f); mm_json_cstr(f,path);
        fprintf(f,",\"index\":%llu,\"name\":",(unsigned long long)i); mm_json_string(f,sp,sl);
        if(has_nid) { fputs(",\"nid\":",f); mm_json_cstr(f,nid); }
        if(sl>12 && sp[11]=='#') {
            const unsigned char *a=sp+12;
            const unsigned char *b=(const unsigned char*)memchr(a,'#',sl-12);
            if(b) {
                fputs(",\"provider_library_token\":",f); mm_json_string(f,a,(size_t)(b-a));
                fputs(",\"provider_module_token\":",f); mm_json_string(f,b+1,(size_t)((sp+sl)-(b+1)));
            }
        }
        fprintf(f,",\"bind\":%u,\"type\":%u,\"shndx\":%u,\"value\":\"0x%016llx\",\"size\":%llu,\"classification\":\"%s\"",
                (unsigned)mm_elf_st_bind(s.st_info),(unsigned)mm_elf_st_type(s.st_info),(unsigned)s.st_shndx,
                (unsigned long long)s.st_value,(unsigned long long)s.st_size,
                is_import?"import":(is_export?"export":"defined"));
        if(!is_import && s.st_value) {
            fprintf(f,",\"runtime_address\":\"0x%016llx\"",(unsigned long long)(obj->mapbase+s.st_value));
        }
        fputs("}\n",f);
        stats->runtime_symbols++;
        if(is_import) stats->runtime_imports++;
        else if(is_export) stats->runtime_exports++;
    }
}

static void mm_scan_runtime_reloc_table(mm_jsonl_t *out, mm_stats_t *stats, uint32_t pid,
                                        const char *path, const char *table,
                                        uint64_t base, uint64_t size,
                                        const mm_dynlib_dynsec_t *ds,
                                        const unsigned char *strtab) {
    if(!base || size<sizeof(mm_elf64_rela_t)) return;
    uint64_t count=size/sizeof(mm_elf64_rela_t);
    if(count>MM_RUNTIME_MAX_RELOCS) {
        mm_emit_runtime_gap(out,stats,pid,path,table,"relocation count exceeded sanity cap",base,size);
        count=MM_RUNTIME_MAX_RELOCS;
    }
    uint64_t symcount=ds->symtabsize/sizeof(mm_elf64_sym_t);
    for(uint64_t i=0;i<count;i++) {
        mm_elf64_rela_t r;
        uint64_t addr=base+i*sizeof(r);
        if(mm_kread(addr,&r,sizeof(r))<0) {
            mm_emit_runtime_gap(out,stats,pid,path,table,"kernel_copyout failed",addr,sizeof(r));
            break;
        }
        uint32_t si=mm_elf64_r_sym(r.r_info);
        FILE *f=out->fp;
        fputs("{\"record\":\"runtime_relocation\",\"pid\":",f); fprintf(f,"%u",pid);
        fputs(",\"path\":",f); mm_json_cstr(f,path); fputs(",\"table\":",f); mm_json_cstr(f,table);
        fprintf(f,",\"index\":%llu,\"offset\":\"0x%016llx\",\"type\":%u,\"symbol_index\":%u,\"addend\":%lld",
                (unsigned long long)i,(unsigned long long)r.r_offset,mm_elf64_r_type(r.r_info),si,(long long)r.r_addend);
        if(strtab && si<symcount) {
            mm_elf64_sym_t s;
            if(mm_kread(ds->symtab+(uint64_t)si*sizeof(s),&s,sizeof(s))==0 && s.st_name<ds->strtabsize) {
                const unsigned char *sp=0; size_t sl=0;
                if(mm_runtime_string(strtab,ds->strtabsize,s.st_name,&sp,&sl)==0 && sl) {
                    fputs(",\"symbol\":",f); mm_json_string(f,sp,sl);
                }
            }
        }
        fputs("}\n",f);
        stats->runtime_relocations++;
    }
}

static void mm_scan_runtime_dynsec(mm_jsonl_t *out, mm_stats_t *stats, uint32_t pid,
                                   const char *path, const mm_dynlib_obj_t *obj) {
    if(!mm_is_kernel_ptr(obj->dynsec)) return;
    mm_dynlib_dynsec_t ds;
    if(mm_kread(obj->dynsec,&ds,sizeof(ds))<0) {
        mm_emit_runtime_gap(out,stats,pid,path,"dynsec","cannot read dynsec",obj->dynsec,sizeof(ds));
        return;
    }
    FILE *f=out->fp;
    fputs("{\"record\":\"runtime_dynsec\",\"pid\":",f); fprintf(f,"%u",pid);
    fputs(",\"path\":",f); mm_json_cstr(f,path);
    fprintf(f,",\"address\":\"0x%016llx\",\"refcount\":%u,\"size\":%llu,"
              "\"symtab\":\"0x%016llx\",\"symtabsize\":%llu,\"strtab\":\"0x%016llx\",\"strtabsize\":%llu,"
              "\"pltrela\":\"0x%016llx\",\"pltrelasize\":%llu,\"rela\":\"0x%016llx\",\"relasize\":%llu,"
              "\"dynamic\":\"0x%016llx\",\"dynamicsize\":%llu,\"sce_dynlib\":\"0x%016llx\",\"sce_dynlibsize\":%llu,"
              "\"hash\":\"0x%016llx\",\"hashsize\":%llu,\"nbuckets\":%u,\"nchains\":%u}\n",
              (unsigned long long)obj->dynsec,ds.refcount,(unsigned long long)ds.size,
              (unsigned long long)ds.symtab,(unsigned long long)ds.symtabsize,
              (unsigned long long)ds.strtab,(unsigned long long)ds.strtabsize,
              (unsigned long long)ds.pltrela,(unsigned long long)ds.pltrelasize,
              (unsigned long long)ds.rela,(unsigned long long)ds.relasize,
              (unsigned long long)ds.dynamic,(unsigned long long)ds.dynamicsize,
              (unsigned long long)ds.sce_dynlib,(unsigned long long)ds.sce_dynlibsize,
              (unsigned long long)ds.hash,(unsigned long long)ds.hashsize,
              ds.nbuckets,ds.nchains);
    stats->runtime_dynsecs++;

    /* Keep the exact loader metadata bytes.  Extended dynlib layouts can vary
     * across firmware revisions; raw preservation lets the PC-side resolver
     * reinterpret fields later without another hardware run. */
    fputs("{\"record\":\"runtime_dynsec_raw\",\"pid\":",f); fprintf(f,"%u",pid);
    fputs(",\"path\":",f); mm_json_cstr(f,path);
    fprintf(f,",\"address\":\"0x%016llx\",\"size\":%llu,\"hex\":",
            (unsigned long long)obj->dynsec,(unsigned long long)sizeof(ds));
    mm_json_hex(f,(const unsigned char*)&ds,sizeof(ds)); fputs("}\n",f);

    unsigned char *strtab=0;
    if(ds.strtab && ds.strtabsize) {
        if(ds.strtabsize>MM_RUNTIME_MAX_STRTAB_SIZE) {
            mm_emit_runtime_gap(out,stats,pid,path,"strtab","string table exceeded sanity cap",ds.strtab,ds.strtabsize);
        } else if(!mm_is_kernel_ptr(ds.strtab)) {
            mm_emit_runtime_gap(out,stats,pid,path,"strtab","string table pointer was not a kernel pointer",ds.strtab,ds.strtabsize);
        } else {
            strtab=(unsigned char*)malloc((size_t)ds.strtabsize);
            if(!strtab) {
                mm_emit_runtime_gap(out,stats,pid,path,"strtab","malloc failed",ds.strtab,ds.strtabsize);
            } else if(mm_kread(ds.strtab,strtab,(size_t)ds.strtabsize)<0) {
                mm_emit_runtime_gap(out,stats,pid,path,"strtab","kernel_copyout failed",ds.strtab,ds.strtabsize);
                free(strtab); strtab=0;
            } else {
                stats->runtime_bytes_copied+=ds.strtabsize;
            }
        }
    }

    mm_scan_runtime_strtab_strings(out,stats,pid,path,strtab,ds.strtabsize);
    mm_scan_runtime_dynamic(out,stats,pid,path,&ds,strtab);
    mm_scan_runtime_symbols(out,stats,pid,path,obj,&ds,strtab);
    mm_scan_runtime_reloc_table(out,stats,pid,path,"rela",ds.rela,ds.relasize,&ds,strtab);
    mm_scan_runtime_reloc_table(out,stats,pid,path,"jmprel",ds.pltrela,ds.pltrelasize,&ds,strtab);

    /* Preserve loader-owned tables that are referenced by pointer. These
     * records are intentionally raw so later firmware-layout research does not
     * require repeating the hardware census. */
    mm_emit_runtime_raw_table(out,stats,pid,path,"sce_comment",ds.sce_comment,ds.sce_commentsize);
    mm_emit_runtime_raw_table(out,stats,pid,path,"sce_dynlib",ds.sce_dynlib,ds.sce_dynlibsize);
    mm_emit_runtime_raw_table(out,stats,pid,path,"hash",ds.hash,ds.hashsize);
    mm_emit_runtime_raw_table(out,stats,pid,path,"buckets",ds.buckets,ds.bucketssize);
    mm_emit_runtime_raw_table(out,stats,pid,path,"chains",ds.chains,ds.chainssize);
    mm_emit_runtime_raw_table(out,stats,pid,path,"unknown1",ds.unknown1,ds.unknown1size);
    for(unsigned i=0;i<7;i++) if(ds.unknown2[i]) {
        FILE *uf=out->fp;
        fputs("{\"record\":\"runtime_unknown_pointer\",\"pid\":",uf); fprintf(uf,"%u",pid);
        fputs(",\"path\":",uf); mm_json_cstr(uf,path);
        fprintf(uf,",\"slot\":%u,\"value\":\"0x%016llx\"}\n",i,(unsigned long long)ds.unknown2[i]);
    }
    free(strtab);
}

static void mm_emit_system_anchors(mm_jsonl_t *out, mm_stats_t *stats) {
    FILE *f=out->fp;
    uint64_t value=0;
    if(KERNEL_ADDRESS_ALLPROC) {
        int rc=mm_kread((uint64_t)KERNEL_ADDRESS_ALLPROC,&value,sizeof(value));
        fputs("{\"record\":\"runtime_system_anchor\",\"kind\":\"allproc\"",f);
        fprintf(f,",\"address\":\"0x%016llx\",\"value\":\"0x%016llx\",\"read_rc\":%d}\n",
                (unsigned long long)KERNEL_ADDRESS_ALLPROC,(unsigned long long)value,rc);
        stats->runtime_system_anchors++;
    }
    value=0;
    if(KERNEL_ADDRESS_ROOTVNODE) {
        int rc=mm_kread((uint64_t)KERNEL_ADDRESS_ROOTVNODE,&value,sizeof(value));
        fputs("{\"record\":\"runtime_system_anchor\",\"kind\":\"rootvnode\"",f);
        fprintf(f,",\"address\":\"0x%016llx\",\"value\":\"0x%016llx\",\"read_rc\":%d}\n",
                (unsigned long long)KERNEL_ADDRESS_ROOTVNODE,(unsigned long long)value,rc);
        stats->runtime_system_anchors++;
    }
    if(KERNEL_ADDRESS_BUS_DATA_DEVICES) {
        unsigned char raw[16]; memset(raw,0,sizeof(raw));
        int rc=mm_kread((uint64_t)KERNEL_ADDRESS_BUS_DATA_DEVICES,raw,sizeof(raw));
        uint64_t first=0; if(rc==0) memcpy(&first,raw,sizeof(first));
        fputs("{\"record\":\"runtime_system_anchor\",\"kind\":\"bus_data_devices\"",f);
        fprintf(f,",\"address\":\"0x%016llx\",\"first_raw\":\"0x%016llx\",\"read_rc\":%d,\"layout\":\"ANCHOR-ONLY\",\"raw16\":",
                (unsigned long long)KERNEL_ADDRESS_BUS_DATA_DEVICES,(unsigned long long)first,rc);
        mm_json_hex(f,raw,sizeof(raw)); fputs("}\n",f);
        stats->runtime_system_anchors++;
    }
}

static void mm_scan_process_threads(mm_jsonl_t *out, mm_stats_t *stats, uint32_t pid,
                                    uint64_t thread_head) {
    if(!thread_head) return;
    if(!mm_is_kernel_ptr(thread_head)) {
        mm_emit_runtime_gap(out,stats,pid,"","threads","thread-list head was not a kernel pointer",thread_head,0);
        return;
    }
    uint64_t thr=thread_head;
    for(uint64_t n=0;n<MM_RUNTIME_MAX_THREADS_PER_PROCESS;n++) {
        uint64_t next=0,tidraw=0;
        if(mm_kread(thr+MM_RUNTIME_THREAD_TID_OFFSET,&tidraw,sizeof(tidraw))<0) {
            mm_emit_runtime_gap(out,stats,pid,"","threads","cannot read thread id field",thr+MM_RUNTIME_THREAD_TID_OFFSET,sizeof(tidraw));
            return;
        }
        if(mm_kread(thr+MM_RUNTIME_THREAD_NEXT_OFFSET,&next,sizeof(next))<0) {
            mm_emit_runtime_gap(out,stats,pid,"","threads","cannot read next thread pointer",thr+MM_RUNTIME_THREAD_NEXT_OFFSET,sizeof(next));
            return;
        }
        FILE *f=out->fp;
        fputs("{\"record\":\"runtime_thread\",\"pid\":",f); fprintf(f,"%u",pid);
        fprintf(f,",\"thread_address\":\"0x%016llx\",\"tid\":%d,\"tid_raw\":\"0x%016llx\",\"next\":\"0x%016llx\",\"layout_source\":\"ps5-payload-dev/sdk kernel_get_proc_thread\"}\n",
                (unsigned long long)thr,(int32_t)tidraw,(unsigned long long)tidraw,(unsigned long long)next);
        stats->runtime_threads++;
        if(!next) return;
        if(next==thr) {
            mm_emit_runtime_gap(out,stats,pid,"","threads","self-referential thread list entry",thr,0);
            return;
        }
        if(!mm_is_kernel_ptr(next)) {
            mm_emit_runtime_gap(out,stats,pid,"","threads","next thread pointer was not a kernel pointer",next,0);
            return;
        }
        thr=next;
    }
    mm_emit_runtime_gap(out,stats,pid,"","threads","thread count reached sanity cap",thread_head,MM_RUNTIME_MAX_THREADS_PER_PROCESS);
}

static void mm_scan_process_vm(mm_jsonl_t *out, mm_stats_t *stats, uint32_t pid,
                               uint64_t vmspace) {
    FILE *f=out->fp;
    if(!vmspace) return;
    if(!mm_is_kernel_ptr(vmspace)) {
        mm_emit_runtime_gap(out,stats,pid,"","vmspace","vmspace was not a kernel pointer",vmspace,0);
        return;
    }
    if(!KERNEL_OFFSET_VMSPACE_P_ROOT) {
        mm_emit_runtime_gap(out,stats,pid,"","vmspace","CRT did not expose KERNEL_OFFSET_VMSPACE_P_ROOT",vmspace,0);
        return;
    }
    uint64_t root=0;
    if(mm_kread(vmspace+(uint64_t)KERNEL_OFFSET_VMSPACE_P_ROOT,&root,sizeof(root))<0) {
        mm_emit_runtime_gap(out,stats,pid,"","vmspace","cannot read VM map root",vmspace+(uint64_t)KERNEL_OFFSET_VMSPACE_P_ROOT,sizeof(root));
        return;
    }
    fputs("{\"record\":\"runtime_vmspace\",\"pid\":",f); fprintf(f,"%u",pid);
    fprintf(f,",\"vmspace\":\"0x%016llx\",\"root_offset\":%llu,\"vm_root\":\"0x%016llx\",\"layout_source\":\"ps5-payload-dev/sdk kernel_get_vmem_entry\"}\n",
            (unsigned long long)vmspace,(unsigned long long)KERNEL_OFFSET_VMSPACE_P_ROOT,(unsigned long long)root);
    stats->runtime_vmspaces++;
    if(!root) return;
    if(!mm_is_kernel_ptr(root)) {
        mm_emit_runtime_gap(out,stats,pid,"","vmspace","VM root was not a kernel pointer",root,0);
        return;
    }

    /* The SDK searches the VM splay/tree through +0x10/+0x18 and then walks
     * consecutive entries through +0x08.  Descend to the leftmost entry first
     * and use that same SDK list-next field for a complete read-only census. */
    uint64_t node=root;
    for(uint64_t depth=0;depth<MM_RUNTIME_MAX_VM_TREE_DEPTH;depth++) {
        uint64_t left=0;
        if(mm_kread(node+MM_RUNTIME_VM_ENTRY_LEFT_OFFSET,&left,sizeof(left))<0) {
            mm_emit_runtime_gap(out,stats,pid,"","vmspace","cannot read VM tree left pointer",node+MM_RUNTIME_VM_ENTRY_LEFT_OFFSET,sizeof(left));
            return;
        }
        if(!left) break;
        if(left==node || !mm_is_kernel_ptr(left)) {
            mm_emit_runtime_gap(out,stats,pid,"","vmspace","invalid VM tree left pointer",left,0);
            return;
        }
        node=left;
        if(depth+1==MM_RUNTIME_MAX_VM_TREE_DEPTH) {
            mm_emit_runtime_gap(out,stats,pid,"","vmspace","VM tree depth reached sanity cap",root,MM_RUNTIME_MAX_VM_TREE_DEPTH);
            return;
        }
    }

    uint64_t prev_start=0;
    int have_prev=0;
    for(uint64_t n=0;n<MM_RUNTIME_MAX_VM_REGIONS_PER_PROCESS;n++) {
        uint64_t next=0,start=0,end=0;
        unsigned char prot=0;
        if(mm_kread(node+MM_RUNTIME_VM_ENTRY_START_OFFSET,&start,sizeof(start))<0 ||
           mm_kread(node+MM_RUNTIME_VM_ENTRY_END_OFFSET,&end,sizeof(end))<0 ||
           mm_kread(node+MM_RUNTIME_VM_ENTRY_PROT_OFFSET,&prot,sizeof(prot))<0 ||
           mm_kread(node+MM_RUNTIME_VM_ENTRY_NEXT_OFFSET,&next,sizeof(next))<0) {
            mm_emit_runtime_gap(out,stats,pid,"","vmspace","cannot read VM entry fields",node,0x68);
            return;
        }
        if(end<=start) {
            mm_emit_runtime_gap(out,stats,pid,"","vmspace","VM entry had non-positive range",node,0);
            return;
        }
        if(have_prev && start<prev_start) {
            mm_emit_runtime_gap(out,stats,pid,"","vmspace","VM entry list became non-monotonic",node,start);
            return;
        }
        fputs("{\"record\":\"runtime_vm_region\",\"pid\":",f); fprintf(f,"%u",pid);
        fprintf(f,",\"entry_address\":\"0x%016llx\",\"start\":\"0x%016llx\",\"end\":\"0x%016llx\",\"size\":%llu,\"prot_raw\":%u,\"next\":\"0x%016llx\"}\n",
                (unsigned long long)node,(unsigned long long)start,(unsigned long long)end,
                (unsigned long long)(end-start),(unsigned)prot,(unsigned long long)next);
        stats->runtime_vm_regions++;
        prev_start=start; have_prev=1;
        if(!next) return;
        /* A vm_map entry chain may close on the vmspace/map sentinel itself.
         * Treat that as a normal list terminator and stop before interpreting
         * the sentinel as a vm_map_entry. */
        if(next==vmspace) {
            fputs("{\"record\":\"runtime_vm_terminator\",\"pid\":",f); fprintf(f,"%u",pid);
            fprintf(f,",\"kind\":\"vmspace_sentinel\",\"address\":\"0x%016llx\"}\n",
                    (unsigned long long)next);
            return;
        }
        if(next==node || !mm_is_kernel_ptr(next)) {
            mm_emit_runtime_gap(out,stats,pid,"","vmspace","invalid VM entry next pointer",next,0);
            return;
        }
        node=next;
    }
    mm_emit_runtime_gap(out,stats,pid,"","vmspace","VM region count reached sanity cap",vmspace,MM_RUNTIME_MAX_VM_REGIONS_PER_PROCESS);
}

static void mm_scan_process_fds(mm_jsonl_t *out, mm_stats_t *stats, uint32_t pid,
                                uint64_t filedesc, uint64_t fd_files) {
    if(!filedesc || !fd_files || !mm_is_kernel_ptr(fd_files)) return;
    int32_t nfiles_raw=0;
    int nrc=mm_kread(fd_files+MM_RUNTIME_FDT_NFILES_OFFSET,&nfiles_raw,sizeof(nfiles_raw));
    FILE *f=out->fp;
    fputs("{\"record\":\"runtime_fd_table\",\"pid\":",f); fprintf(f,"%u",pid);
    fprintf(f,",\"filedesc\":\"0x%016llx\",\"fd_files\":\"0x%016llx\",\"nfiles_raw\":%d,\"read_rc\":%d,\"entries_offset\":%llu,\"entry_stride\":%llu,\"layout_source\":\"ps5-payload-dev/sdk kernel_get_proc_file + FreeBSD fdescenttbl\"}\n",
            (unsigned long long)filedesc,(unsigned long long)fd_files,nfiles_raw,nrc,
            (unsigned long long)MM_RUNTIME_FDT_ENTRIES_OFFSET,(unsigned long long)MM_RUNTIME_FDE_STRIDE);
    stats->runtime_fd_tables++;
    if(nrc<0) {
        mm_emit_runtime_limit(out,stats,"fd_table","cannot read fdt_nfiles; FD entries not guessed",fd_files,sizeof(nfiles_raw));
        return;
    }
    if(nfiles_raw<0 || (uint64_t)nfiles_raw>MM_RUNTIME_MAX_FDS_PER_PROCESS) {
        mm_emit_runtime_limit(out,stats,"fd_table","fdt_nfiles failed sanity validation; FD entries not guessed",fd_files,(uint64_t)(uint32_t)nfiles_raw);
        return;
    }
    for(uint64_t fd=0;fd<(uint64_t)nfiles_raw;fd++) {
        uint64_t entry=fd_files+MM_RUNTIME_FDT_ENTRIES_OFFSET+MM_RUNTIME_FDE_STRIDE*fd;
        uint64_t fde_file=0;
        if(mm_kread(entry+MM_RUNTIME_FDE_FILE_OFFSET,&fde_file,sizeof(fde_file))<0) {
            mm_emit_runtime_limit(out,stats,"fd_table","cannot read fde_file; remaining table left explicit",entry,sizeof(fde_file));
            return;
        }
        if(!fde_file) continue;
        if(!mm_is_kernel_ptr(fde_file)) {
            fputs("{\"record\":\"runtime_fd\",\"pid\":",f); fprintf(f,"%u",pid);
            fprintf(f,",\"fd\":%llu,\"entry_address\":\"0x%016llx\",\"fde_file\":\"0x%016llx\",\"file_data\":\"0x%016llx\",\"file_data_read_rc\":-1,\"layout_confidence\":\"INVALID-FDE-POINTER\"}\n",
                    (unsigned long long)fd,(unsigned long long)entry,(unsigned long long)fde_file,0ULL);
            stats->runtime_fds++;
            continue;
        }
        /* Exact behavior of SDK kernel_get_proc_file: first qword at fde_file.
         * We deliberately do not walk credential-bearing struct-file internals. */
        uint64_t file_data=0;
        int drc=mm_kread(fde_file,&file_data,sizeof(file_data));
        fputs("{\"record\":\"runtime_fd\",\"pid\":",f); fprintf(f,"%u",pid);
        fprintf(f,",\"fd\":%llu,\"entry_address\":\"0x%016llx\",\"fde_file\":\"0x%016llx\",\"file_data\":\"0x%016llx\",\"file_data_read_rc\":%d,\"layout_source\":\"ps5-payload-dev/sdk kernel_get_proc_file\",\"policy\":\"no struct-file credential decode\"}\n",
                (unsigned long long)fd,(unsigned long long)entry,(unsigned long long)fde_file,
                (unsigned long long)file_data,drc);
        stats->runtime_fds++;

        /* The public PS5 SDK gives us the first qword at fde_file as the
         * resource/data object.  Preserve only a bounded raw prefix of that
         * object.  We deliberately do not decode struct-file credentials and
         * do not dereference arbitrary pointers found inside this raw data. */
        if(drc==0 && mm_is_kernel_ptr(file_data)) {
            unsigned char raw[MM_RUNTIME_FD_DATA_RAW_SIZE];
            int rrc=mm_kread(file_data,raw,sizeof(raw));
            fputs("{\"record\":\"runtime_fd_data_raw\",\"pid\":",f); fprintf(f,"%u",pid);
            fprintf(f,",\"fd\":%llu,\"file_data\":\"0x%016llx\",\"size\":%llu,\"read_rc\":%d,\"interpretation\":\"RAW-RESOURCE-OBJECT-NO-SEMANTIC-DECODE\"",
                    (unsigned long long)fd,(unsigned long long)file_data,
                    (unsigned long long)sizeof(raw),rrc);
            if(rrc==0) { fputs(",\"hex\":",f); mm_json_hex(f,raw,sizeof(raw)); }
            fputs("}\n",f);
            stats->runtime_fd_data_raw++;
            if(rrc==0) {
                stats->runtime_fd_data_raw_bytes+=sizeof(raw);
                for(uint64_t off=0;off+8<=sizeof(raw);off+=8) {
                    uint64_t q=0; memcpy(&q,raw+(size_t)off,8);
                    if(!mm_is_kernel_ptr(q)) continue;
                    fputs("{\"record\":\"runtime_fd_data_pointer_candidate\",\"pid\":",f); fprintf(f,"%u",pid);
                    fprintf(f,",\"fd\":%llu,\"file_data\":\"0x%016llx\",\"offset\":%llu,\"pointer\":\"0x%016llx\",\"evidence\":\"raw aligned qword only; pointer not dereferenced\"}\n",
                            (unsigned long long)fd,(unsigned long long)file_data,
                            (unsigned long long)off,(unsigned long long)q);
                    stats->runtime_fd_data_pointer_candidates++;
                }
            }
        }
    }
}

typedef struct mm_bus_driver_seen {
    uint64_t address;
} mm_bus_driver_seen_t;

static int mm_bus_driver_was_seen(mm_bus_driver_seen_t *seen, size_t n, uint64_t addr) {
    for(size_t i=0;i<n;i++) if(seen[i].address==addr) return 1;
    return 0;
}

static int mm_is_reasonable_text(const char *s) {
    if(!s || !s[0]) return 0;
    size_t n=mm_bounded_strlen(s,MM_RUNTIME_MAX_PATH);
    if(n==0 || n>=MM_RUNTIME_MAX_PATH-1) return 0;
    for(size_t i=0;i<n;i++) {
        unsigned char c=(unsigned char)s[i];
        if(c<0x20 || c>0x7e) return 0;
    }
    return 1;
}

static void mm_scan_bus_driver(mm_jsonl_t *out, mm_stats_t *stats, uint64_t driver,
                               mm_bus_driver_seen_t *seen, size_t *seen_count) {
    if(!driver || !mm_is_kernel_ptr(driver)) return;
    if(mm_bus_driver_was_seen(seen,*seen_count,driver)) return;
    if(*seen_count<MM_RUNTIME_MAX_BUS_DEVICES) seen[(*seen_count)++].address=driver;

    uint64_t nameptr=0,methods=0,class_size=0,baseclasses=0,ops=0;
    uint32_t refs=0;
    if(mm_kread(driver+0x00,&nameptr,8)<0 || mm_kread(driver+0x08,&methods,8)<0 ||
       mm_kread(driver+0x10,&class_size,8)<0 || mm_kread(driver+0x18,&baseclasses,8)<0 ||
       mm_kread(driver+0x20,&refs,4)<0 || mm_kread(driver+0x28,&ops,8)<0) {
        mm_emit_runtime_limit(out,stats,"bus_driver","cannot read kobj_class prefix",driver,0x30);
        return;
    }
    char name[MM_RUNTIME_MAX_PATH]; name[0]=0;
    int nrc=mm_kread_cstr(nameptr,name);
    if(nrc<0 || !mm_is_reasonable_text(name)) {
        mm_emit_runtime_limit(out,stats,"bus_driver","driver class name failed validation; method table not guessed",driver,0x30);
        return;
    }
    FILE *f=out->fp;
    fputs("{\"record\":\"runtime_bus_driver\",\"driver_address\":",f); fprintf(f,"\"0x%016llx\"",(unsigned long long)driver);
    fputs(",\"name\":",f); mm_json_cstr(f,name);
    fprintf(f,",\"methods\":\"0x%016llx\",\"class_size_raw\":%llu,\"baseclasses\":\"0x%016llx\",\"refs_raw\":%u,\"ops\":\"0x%016llx\",\"layout_source\":\"FreeBSD kobj_class; runtime string validated\"}\n",
            (unsigned long long)methods,(unsigned long long)class_size,(unsigned long long)baseclasses,
            refs,(unsigned long long)ops);
    stats->runtime_bus_drivers++;
    if(!methods) return;
    if(!mm_is_kernel_ptr(methods)) {
        mm_emit_runtime_limit(out,stats,"bus_driver_methods","method table pointer failed validation",methods,0);
        return;
    }
    for(uint64_t i=0;i<MM_RUNTIME_MAX_DRIVER_METHODS;i++) {
        uint64_t desc=0,func=0;
        uint64_t ma=methods+i*16ULL;
        if(mm_kread(ma,&desc,8)<0 || mm_kread(ma+8,&func,8)<0) {
            mm_emit_runtime_limit(out,stats,"bus_driver_methods","cannot read bounded method entry",ma,16);
            return;
        }
        if(!desc && !func) return;
        uint32_t desc_id=0; int idrc=-1;
        uint64_t default_func=0;
        if(desc && mm_is_kernel_ptr(desc)) {
            idrc=mm_kread(desc,&desc_id,sizeof(desc_id));
            (void)mm_kread(desc+16,&default_func,sizeof(default_func));
        }
        fputs("{\"record\":\"runtime_bus_driver_method\",\"driver_address\":",f); fprintf(f,"\"0x%016llx\"",(unsigned long long)driver);
        fputs(",\"driver_name\":",f); mm_json_cstr(f,name);
        fprintf(f,",\"index\":%llu,\"method_address\":\"0x%016llx\",\"desc\":\"0x%016llx\",\"desc_id_raw\":%u,\"desc_id_read_rc\":%d,\"func\":\"0x%016llx\",\"default_func_raw\":\"0x%016llx\"",
                (unsigned long long)i,(unsigned long long)ma,(unsigned long long)desc,desc_id,idrc,
                (unsigned long long)func,(unsigned long long)default_func);
        if(KERNEL_ADDRESS_TEXT_BASE && func>=(uint64_t)KERNEL_ADDRESS_TEXT_BASE)
            fprintf(f,",\"kernel_text_offset\":\"0x%016llx\"",(unsigned long long)(func-(uint64_t)KERNEL_ADDRESS_TEXT_BASE));
        fputs(",\"layout_source\":\"FreeBSD kobj_method/kobjop_desc; bounded read-only\"}\n",f);
        stats->runtime_bus_driver_methods++;
    }
    mm_emit_runtime_limit(out,stats,"bus_driver_methods","method count reached sanity cap",methods,MM_RUNTIME_MAX_DRIVER_METHODS);
}

static void mm_emit_bus_candidate_strings(mm_jsonl_t *out, mm_stats_t *stats,
                                          uint64_t dev, const unsigned char raw[MM_BUS_DEV_PREFIX_SIZE]) {
    FILE *f=out->fp;
    uint64_t emitted_ptrs[MM_BUS_DEV_PREFIX_SIZE/8];
    size_t emitted_count=0;
    for(uint64_t off=0; off+8<=MM_BUS_DEV_PREFIX_SIZE; off+=8) {
        uint64_t ptr=0;
        memcpy(&ptr,raw+off,8);
        if(!mm_is_kernel_ptr(ptr)) continue;
        int dup=0;
        for(size_t i=0;i<emitted_count;i++) if(emitted_ptrs[i]==ptr) { dup=1; break; }
        if(dup) continue;
        char text[MM_RUNTIME_MAX_PATH]; text[0]=0;
        if(mm_kread_cstr(ptr,text)<0 || !mm_is_reasonable_text(text)) continue;
        if(emitted_count<MM_BUS_DEV_PREFIX_SIZE/8) emitted_ptrs[emitted_count++]=ptr;
        fputs("{\"record\":\"runtime_bus_candidate_string\",\"device_address\":",f);
        fprintf(f,"\"0x%016llx\",\"offset\":%llu,\"pointer\":\"0x%016llx\",\"value\":",
                (unsigned long long)dev,(unsigned long long)off,(unsigned long long)ptr);
        mm_json_cstr(f,text);
        fputs(",\"evidence\":\"bounded qword pointer -> readable ASCII; semantic field not assigned\"}\n",f);
        stats->runtime_bus_candidate_strings++;
    }
}

static void mm_emit_bus_terminator(mm_jsonl_t *out, mm_stats_t *stats, uint64_t dev,
                                   uint64_t expected_last, const char *kind, int tail_match) {
    FILE *f=out->fp;
    fputs("{\"record\":\"runtime_bus_list_terminator\",\"device_address\":",f);
    fprintf(f,"\"0x%016llx\",\"expected_last_device\":\"0x%016llx\",\"kind\":",
            (unsigned long long)dev,(unsigned long long)expected_last);
    mm_json_cstr(f,kind?kind:"");
    fprintf(f,",\"matches_tailq_expected_last\":%s}\n",tail_match?"true":"false");
    stats->runtime_bus_list_terminators++;
}

static void mm_scan_kernel_bus(mm_jsonl_t *out, mm_stats_t *stats) {
    if(!KERNEL_ADDRESS_BUS_DATA_DEVICES) return;
    uint64_t head_before[2]={0,0};
    if(mm_kread((uint64_t)KERNEL_ADDRESS_BUS_DATA_DEVICES,head_before,sizeof(head_before))<0) {
        mm_emit_runtime_limit(out,stats,"bus_devices","cannot read bus_data_devices TAILQ head",(uint64_t)KERNEL_ADDRESS_BUS_DATA_DEVICES,sizeof(head_before));
        return;
    }
    uint64_t dev=head_before[0];
    uint64_t tail_next_slot=head_before[1];
    uint64_t expected_last=(mm_is_kernel_ptr(tail_next_slot) && tail_next_slot>=MM_BUS_DEV_NEXT)?tail_next_slot-MM_BUS_DEV_NEXT:0;
    FILE *f=out->fp;
    fputs("{\"record\":\"runtime_bus_list_head\",\"address\":",f);
    fprintf(f,"\"0x%016llx\",\"first\":\"0x%016llx\",\"tail_next_slot\":\"0x%016llx\",\"expected_last_device\":\"0x%016llx\",\"layout_source\":\"FreeBSD TAILQ_HEAD lineage + PS5-source-grounded global-next +0x18\"}\n",
            (unsigned long long)KERNEL_ADDRESS_BUS_DATA_DEVICES,(unsigned long long)dev,
            (unsigned long long)tail_next_slot,(unsigned long long)expected_last);
    if(!dev) {
        mm_emit_bus_terminator(out,stats,0,expected_last,"empty_list",expected_last==0);
        return;
    }
    if(!mm_is_kernel_ptr(dev)) {
        mm_emit_runtime_limit(out,stats,"bus_devices","bus_data_devices first pointer failed kernel-pointer validation",dev,0);
        return;
    }
    mm_bus_driver_seen_t *seen=calloc(MM_RUNTIME_MAX_BUS_DEVICES,sizeof(*seen));
    uint64_t *dev_seen=calloc(MM_RUNTIME_MAX_BUS_DEVICES,sizeof(*dev_seen));
    size_t seen_count=0,dev_seen_count=0;
    if(!seen || !dev_seen) {
        free(seen); free(dev_seen);
        mm_emit_runtime_limit(out,stats,"bus_devices","cannot allocate bus de-dup tables",0,0);
        return;
    }
    int ended=0;
    for(uint64_t i=0;i<MM_RUNTIME_MAX_BUS_DEVICES;i++) {
        int cycle=0;
        for(size_t si=0;si<dev_seen_count;si++) if(dev_seen[si]==dev) { cycle=1; break; }
        if(cycle) {
            mm_emit_runtime_limit(out,stats,"bus_devices","global device list cycle detected; stopped without guessing",dev,0);
            break;
        }
        dev_seen[dev_seen_count++]=dev;
        stats->runtime_bus_nodes_walked++;

        unsigned char raw[MM_BUS_DEV_PREFIX_SIZE];
        if(mm_kread(dev,raw,sizeof(raw))<0) {
            mm_emit_runtime_limit(out,stats,"bus_devices","cannot read _device/raw-node prefix",dev,sizeof(raw));
            break;
        }
        uint64_t next=0,parent=0,driver=0,devclass=0,nameptr=0,descptr=0,ivars=0,softc=0;
        uint32_t unit=0,busy=0,state=0,devflags=0,flags=0,order=0;
        memcpy(&next,raw+MM_BUS_DEV_NEXT,8); memcpy(&parent,raw+MM_BUS_DEV_PARENT,8);
        memcpy(&driver,raw+MM_BUS_DEV_DRIVER,8); memcpy(&devclass,raw+MM_BUS_DEV_DEVCLASS,8);
        memcpy(&unit,raw+MM_BUS_DEV_UNIT,4); memcpy(&nameptr,raw+MM_BUS_DEV_NAMEUNIT,8);
        memcpy(&descptr,raw+MM_BUS_DEV_DESC,8); memcpy(&busy,raw+MM_BUS_DEV_BUSY,4);
        memcpy(&state,raw+MM_BUS_DEV_STATE,4); memcpy(&devflags,raw+MM_BUS_DEV_DEVFLAGS,4);
        memcpy(&flags,raw+MM_BUS_DEV_FLAGS,4); memcpy(&order,raw+MM_BUS_DEV_ORDER,4);
        memcpy(&ivars,raw+MM_BUS_DEV_IVARS,8); memcpy(&softc,raw+MM_BUS_DEV_SOFTC,8);
        char name[MM_RUNTIME_MAX_PATH]; name[0]=0;
        char desc[MM_RUNTIME_MAX_PATH]; desc[0]=0;
        int name_rc=mm_kread_cstr(nameptr,name);
        int desc_rc=descptr?mm_kread_cstr(descptr,desc):-1;
        int decoded=(name_rc==0 && mm_is_reasonable_text(name));

        if(!decoded) {
            fputs("{\"record\":\"runtime_bus_device_raw_candidate\",\"index\":",f); fprintf(f,"%llu",(unsigned long long)i);
            fprintf(f,",\"device_address\":\"0x%016llx\",\"next_ps5_confirmed\":\"0x%016llx\",\"nameunit_pointer_at_0x58\":\"0x%016llx\",\"nameunit_read_rc\":%d,\"expected_last_device\":\"0x%016llx\",\"reason\":\"nameunit did not validate; semantic _device decode withheld, raw node retained and list continues only via PS5-confirmed +0x18 next\",\"raw_prefix\":",
                    (unsigned long long)dev,(unsigned long long)next,(unsigned long long)nameptr,name_rc,(unsigned long long)expected_last);
            mm_json_hex(f,raw,sizeof(raw)); fputs("}\n",f);
            stats->runtime_bus_raw_candidates++;
            mm_emit_bus_candidate_strings(out,stats,dev,raw);
        } else {
            fputs("{\"record\":\"runtime_bus_device\",\"index\":",f); fprintf(f,"%llu",(unsigned long long)i);
            fprintf(f,",\"device_address\":\"0x%016llx\",\"next\":\"0x%016llx\",\"parent\":\"0x%016llx\",\"driver\":\"0x%016llx\",\"devclass\":\"0x%016llx\",\"unit_raw\":%u,\"nameunit\":",
                    (unsigned long long)dev,(unsigned long long)next,(unsigned long long)parent,(unsigned long long)driver,
                    (unsigned long long)devclass,unit);
            mm_json_cstr(f,name); fputs(",\"desc\":",f); if(desc_rc==0 && mm_is_reasonable_text(desc)) mm_json_cstr(f,desc); else mm_json_cstr(f,"");
            fprintf(f,",\"busy_raw\":%u,\"state_raw\":%u,\"devflags_raw\":%u,\"flags_raw\":%u,\"order_raw\":%u,\"ivars\":\"0x%016llx\",\"softc\":\"0x%016llx\",\"layout_confidence\":\"PS5-SOURCE-GROUNDED-NAMEUNIT-SOFTC-NEXT; FREEBSD-LINEAGE-PREFIX\",\"raw_prefix\":",
                    busy,state,devflags,flags,order,(unsigned long long)ivars,(unsigned long long)softc);
            mm_json_hex(f,raw,sizeof(raw)); fputs("}\n",f);
            stats->runtime_bus_devices++;

            if(softc && mm_is_kernel_ptr(softc)) {
                unsigned char sr[MM_RUNTIME_BUS_SOFTC_HEAD];
                if(mm_kread(softc,sr,sizeof(sr))==0) {
                    fputs("{\"record\":\"runtime_bus_softc_head\",\"device_address\":",f); fprintf(f,"\"0x%016llx\"",(unsigned long long)dev);
                    fputs(",\"nameunit\":",f); mm_json_cstr(f,name);
                    fprintf(f,",\"softc\":\"0x%016llx\",\"size\":%llu,\"interpretation\":\"RAW-UNKNOWN-READ-ONLY\",\"hex\":",
                            (unsigned long long)softc,(unsigned long long)sizeof(sr));
                    mm_json_hex(f,sr,sizeof(sr)); fputs("}\n",f);
                    stats->runtime_bus_softc_heads++; stats->runtime_bus_softc_bytes+=sizeof(sr);
                }
            }
            mm_scan_bus_driver(out,stats,driver,seen,&seen_count);
        }

        if(!next) {
            int tail_match=(expected_last==0 || dev==expected_last);
            mm_emit_bus_terminator(out,stats,dev,expected_last,decoded?"null_next_decoded":"null_next_raw_candidate",tail_match);
            if(expected_last && dev!=expected_last)
                mm_emit_runtime_limit(out,stats,"bus_devices","list ended before TAILQ head expected-last pointer; snapshot may have changed",dev,0);
            ended=1;
            break;
        }
        if(next==dev || !mm_is_kernel_ptr(next)) {
            mm_emit_runtime_limit(out,stats,"bus_devices","invalid PS5-confirmed global-next pointer; walk stopped",next,0);
            break;
        }
        dev=next;
        if(i+1==MM_RUNTIME_MAX_BUS_DEVICES)
            mm_emit_runtime_limit(out,stats,"bus_devices","device count reached sanity cap",dev,MM_RUNTIME_MAX_BUS_DEVICES);
    }

    uint64_t head_after[2]={0,0};
    int hrc=mm_kread((uint64_t)KERNEL_ADDRESS_BUS_DATA_DEVICES,head_after,sizeof(head_after));
    int stable=(hrc==0 && head_after[0]==head_before[0] && head_after[1]==head_before[1]);
    fputs("{\"record\":\"runtime_bus_list_consistency\",\"head_read_rc\":",f); fprintf(f,"%d",hrc);
    fprintf(f,",\"first_before\":\"0x%016llx\",\"first_after\":\"0x%016llx\",\"tail_slot_before\":\"0x%016llx\",\"tail_slot_after\":\"0x%016llx\",\"stable\":%s,\"ended_with_null\":%s}\n",
            (unsigned long long)head_before[0],(unsigned long long)head_after[0],
            (unsigned long long)head_before[1],(unsigned long long)head_after[1],
            stable?"true":"false",ended?"true":"false");
    if(stable) stats->runtime_bus_snapshot_stable++;
    free(seen); free(dev_seen);
}

static void mm_scan_process_resources(mm_jsonl_t *out, mm_stats_t *stats, uint32_t pid,
                                      uint64_t proc_addr) {
    uint64_t thread_head=0,filedesc=0,fd_files=0,root_vnode=0,jail_vnode=0,vmspace=0,dynlib_head=0;
    int thread_rc=mm_kread(proc_addr+MM_RUNTIME_PROC_THREAD_HEAD_OFFSET,&thread_head,sizeof(thread_head));
    int fd_rc=mm_kread(proc_addr+(uint64_t)KERNEL_OFFSET_PROC_P_FD,&filedesc,sizeof(filedesc));
    int vm_rc=mm_kread(proc_addr+(uint64_t)KERNEL_OFFSET_PROC_P_VMSPACE,&vmspace,sizeof(vmspace));
    int dyn_rc=mm_kread(proc_addr+MM_RUNTIME_PROC_DYNLIB_HEAD_OFFSET,&dynlib_head,sizeof(dynlib_head));
    if(fd_rc==0 && filedesc && mm_is_kernel_ptr(filedesc)) {
        (void)mm_kread(filedesc,&fd_files,sizeof(fd_files));
        (void)mm_kread(filedesc+(uint64_t)KERNEL_OFFSET_FILEDESC_FD_RDIR,&root_vnode,sizeof(root_vnode));
        (void)mm_kread(filedesc+(uint64_t)KERNEL_OFFSET_FILEDESC_FD_JDIR,&jail_vnode,sizeof(jail_vnode));
        if(fd_files) stats->runtime_resource_anchors++;
        if(root_vnode) stats->runtime_resource_anchors++;
        if(jail_vnode) stats->runtime_resource_anchors++;
    }
    if(vmspace) stats->runtime_resource_anchors++;
    if(thread_head) stats->runtime_resource_anchors++;

    FILE *f=out->fp;
    fputs("{\"record\":\"runtime_process_resources\",\"pid\":",f); fprintf(f,"%u",pid);
    fprintf(f,",\"proc_address\":\"0x%016llx\",\"thread_head\":\"0x%016llx\",\"filedesc\":\"0x%016llx\",\"fd_files\":\"0x%016llx\",\"root_vnode\":\"0x%016llx\",\"jail_vnode\":\"0x%016llx\",\"vmspace\":\"0x%016llx\",\"dynlib_list_head\":\"0x%016llx\",\"thread_read_rc\":%d,\"filedesc_read_rc\":%d,\"vmspace_read_rc\":%d,\"dynlib_read_rc\":%d,\"policy\":\"read-only anchors; credentials excluded\"}\n",
            (unsigned long long)proc_addr,(unsigned long long)thread_head,(unsigned long long)filedesc,
            (unsigned long long)fd_files,(unsigned long long)root_vnode,(unsigned long long)jail_vnode,
            (unsigned long long)vmspace,(unsigned long long)dynlib_head,thread_rc,fd_rc,vm_rc,dyn_rc);
    stats->runtime_process_resources++;

    if(fd_rc==0) mm_scan_process_fds(out,stats,pid,filedesc,fd_files);
    if(thread_rc==0) mm_scan_process_threads(out,stats,pid,thread_head);
    if(vm_rc==0) mm_scan_process_vm(out,stats,pid,vmspace);
}

static void mm_scan_process_modules(mm_jsonl_t *out, mm_stats_t *stats, uint32_t pid, uint64_t proc_addr) {
    uint64_t list_head=0;
    if(mm_kread(proc_addr+MM_RUNTIME_PROC_DYNLIB_HEAD_OFFSET,&list_head,sizeof(list_head))<0) {
        mm_emit_runtime_gap(out,stats,pid,"","dynlib_list","cannot read process dynlib list head",proc_addr+MM_RUNTIME_PROC_DYNLIB_HEAD_OFFSET,sizeof(list_head));
        return;
    }
    FILE *f=out->fp;
    fputs("{\"record\":\"runtime_process\",\"pid\":",f); fprintf(f,"%u",pid);
    fprintf(f,",\"proc_address\":\"0x%016llx\",\"dynlib_list_head\":\"0x%016llx\"}\n",
            (unsigned long long)proc_addr,(unsigned long long)list_head);
    stats->runtime_processes++;

    if(!mm_is_kernel_ptr(list_head)) return;
    uint64_t cursor=list_head;
    for(uint64_t n=0;n<MM_RUNTIME_MAX_MODULES_PER_PROCESS;n++) {
        uint64_t obj_addr=0;
        if(mm_kread(cursor,&obj_addr,sizeof(obj_addr))<0) {
            mm_emit_runtime_gap(out,stats,pid,"","dynlib_list","cannot read next dynlib object pointer",cursor,sizeof(obj_addr));
            break;
        }
        if(!obj_addr) break;
        if(!mm_is_kernel_ptr(obj_addr)) {
            mm_emit_runtime_gap(out,stats,pid,"","dynlib_list","next dynlib object was not a kernel pointer",obj_addr,0);
            break;
        }
        mm_dynlib_obj_t obj;
        if(mm_kread(obj_addr,&obj,sizeof(obj))<0) {
            mm_emit_runtime_gap(out,stats,pid,"","dynlib_object","cannot read dynlib object",obj_addr,sizeof(obj));
            break;
        }
        char path[MM_RUNTIME_MAX_PATH];
        if(mm_kread_cstr(obj.path,path)<0) snprintf(path,sizeof(path),"<unreadable-path@0x%016llx>",(unsigned long long)obj.path);
        size_t plen=mm_bounded_strlen(path,sizeof(path)); path[plen<sizeof(path)?plen:sizeof(path)-1]=0;

        int ext_sane=mm_obj_extended_layout_sane(&obj);
        fputs("{\"record\":\"runtime_module\",\"pid\":",f); fprintf(f,"%u",pid);
        fputs(",\"path\":",f); mm_json_cstr(f,path);
        fprintf(f,",\"object_address\":\"0x%016llx\",\"handle\":\"0x%016llx\",\"refcount\":%u,"
                  "\"mapbase\":\"0x%016llx\",\"mapsize\":%llu,\"textsize_raw\":%llu,"
                  "\"database_raw\":\"0x%016llx\",\"datasize_raw\":%llu,\"entry_raw\":\"0x%016llx\",\"vaddrbase_raw\":\"0x%016llx\","
                  "\"tlsindex_raw\":%u,\"tlsinit_raw\":\"0x%016llx\",\"tlsinitsize_raw\":%llu,\"tlssize_raw\":%llu,\"tlsoffset_raw\":%llu,\"tlsalign_raw\":%llu,"
                  "\"pltgot_raw\":\"0x%016llx\",\"init_raw\":\"0x%016llx\",\"fini_raw\":\"0x%016llx\",\"status_raw\":%d,\"flags_raw\":%d,\"dynsec\":\"0x%016llx\","
                  "\"prefix_layout_confidence\":\"HIGH\",\"extended_layout_sane\":%s,\"extended_layout_confidence\":\"%s\"}\n",
                  (unsigned long long)obj_addr,(unsigned long long)obj.handle,obj.refcount,
                  (unsigned long long)obj.mapbase,(unsigned long long)obj.mapsize,(unsigned long long)obj.textsize,
                  (unsigned long long)obj.database,(unsigned long long)obj.datasize,(unsigned long long)obj.entry,(unsigned long long)obj.vaddrbase,
                  obj.tlsindex,(unsigned long long)obj.tlsinit,(unsigned long long)obj.tlsinitsize,(unsigned long long)obj.tlssize,
                  (unsigned long long)obj.tlsoffset,(unsigned long long)obj.tlsalign,
                  (unsigned long long)obj.pltgot,(unsigned long long)obj.init,(unsigned long long)obj.fini,
                  obj.status,obj.flags,(unsigned long long)obj.dynsec,
                  ext_sane?"true":"false",ext_sane?"PLAUSIBLE":"UNVERIFIED-FW-LAYOUT");

        fputs("{\"record\":\"runtime_object_raw\",\"pid\":",f); fprintf(f,"%u",pid);
        fputs(",\"path\":",f); mm_json_cstr(f,path);
        fprintf(f,",\"object_address\":\"0x%016llx\",\"size\":%llu,\"hex\":",
                (unsigned long long)obj_addr,(unsigned long long)sizeof(obj));
        mm_json_hex(f,(const unsigned char*)&obj,sizeof(obj)); fputs("}\n",f);
        stats->runtime_modules++;

        mm_scan_runtime_dynsec(out,stats,pid,path,&obj);
        cursor=obj_addr;
    }
}
#endif

int mm_runtime_scan_all_processes(mm_jsonl_t *out, mm_stats_t *stats) {
#if MM_RUNTIME_HAVE_PS5
    FILE *f=out->fp;
    fprintf(f,"{\"record\":\"runtime_scan_header\",\"available\":true,\"allproc\":\"0x%016llx\",\"proc_pid_offset\":%llu,\"proc_fd_offset\":%llu,\"proc_vmspace_offset\":%llu,\"proc_thread_head_offset\":%llu,\"proc_dynlib_head_offset\":%llu,\"vmspace_root_offset\":%llu,\"policy\":\"deep read-only process/thread/vm/fd + kernel-bus/device/driver graph\"}\n",
            (unsigned long long)KERNEL_ADDRESS_ALLPROC,
            (unsigned long long)KERNEL_OFFSET_PROC_P_PID,
            (unsigned long long)KERNEL_OFFSET_PROC_P_FD,
            (unsigned long long)KERNEL_OFFSET_PROC_P_VMSPACE,
            (unsigned long long)MM_RUNTIME_PROC_THREAD_HEAD_OFFSET,
            (unsigned long long)MM_RUNTIME_PROC_DYNLIB_HEAD_OFFSET,
            (unsigned long long)KERNEL_OFFSET_VMSPACE_P_ROOT);
    mm_emit_system_anchors(out,stats);
    mm_scan_kernel_bus(out,stats);
    if(!KERNEL_ADDRESS_ALLPROC) {
        mm_emit_runtime_gap(out,stats,0,"","allproc","KERNEL_ADDRESS_ALLPROC is zero on this CRT/firmware",0,0);
        return -1;
    }
    uint64_t proc=0;
    if(kernel_copyout(KERNEL_ADDRESS_ALLPROC,&proc,sizeof(proc))<0) {
        mm_emit_runtime_gap(out,stats,0,"","allproc","cannot read allproc head",KERNEL_ADDRESS_ALLPROC,sizeof(proc));
        return -1;
    }
    uint64_t seen=0;
    while(proc && seen<MM_RUNTIME_MAX_PROCESSES) {
        if(!mm_is_kernel_ptr(proc)) {
            mm_emit_runtime_gap(out,stats,0,"","allproc","process pointer was not a kernel pointer",proc,0);
            break;
        }
        uint32_t pid=0;
        uint64_t next=0;
        if(mm_kread(proc+(uint64_t)KERNEL_OFFSET_PROC_P_PID,&pid,sizeof(pid))<0) {
            mm_emit_runtime_gap(out,stats,0,"","allproc","cannot read process pid",proc+(uint64_t)KERNEL_OFFSET_PROC_P_PID,sizeof(pid));
            break;
        }
        if(mm_kread(proc,&next,sizeof(next))<0) {
            mm_emit_runtime_gap(out,stats,pid,"","allproc","cannot read next process pointer",proc,sizeof(next));
            break;
        }
        mm_scan_process_resources(out,stats,pid,proc);
        mm_scan_process_modules(out,stats,pid,proc);
        seen++;
        if(next==proc) {
            mm_emit_runtime_gap(out,stats,pid,"","allproc","self-referential process list entry",proc,0);
            break;
        }
        proc=next;
    }
    if(seen>=MM_RUNTIME_MAX_PROCESSES) {
        mm_emit_runtime_gap(out,stats,0,"","allproc","process count reached sanity cap",0,seen);
        return -1;
    }
    return 0;
#else
    FILE *f=out->fp;
    fputs("{\"record\":\"runtime_scan_header\",\"available\":false,\"reason\":\"ps5/kernel.h unavailable in host validation build\"}\n",f);
    (void)stats;
    return 0;
#endif
}

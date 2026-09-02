#define _POSIX_C_SOURCE 200809L
#include "mm_mapper.h"
#include "mm_sha256.h"
#include "mm_elf.h"
#include "mm_runtime.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <limits.h>
#include <sys/utsname.h>

#if defined(__has_include)
# if __has_include(<ps5/kernel.h>)
#  include <ps5/kernel.h>
#  define MM_HAVE_PS5_KERNEL 1
# endif
#endif
#ifndef MM_HAVE_PS5_KERNEL
#define MM_HAVE_PS5_KERNEL 0
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MM_VERSION "0.8-RESOURCE-CHAIN-CORRELATION-GRAPH"
#define MM_OUTDIR "/data/MM_PS5_API_MAP"
#define MM_MAX_FILE_SIZE (512ULL * 1024ULL * 1024ULL)
#define MM_MAX_DEPTH 64
#define MM_MAX_DEVICE_DEPTH 8

static mm_stats_t g_stats;
static mm_jsonl_t g_json;

static void mm_json_hex(FILE *f, const unsigned char *p, size_t n) {
    static const char h[]="0123456789abcdef";
    fputc('"',f);
    for(size_t i=0;i<n;i++) { fputc(h[p[i]>>4],f); fputc(h[p[i]&15],f); }
    fputc('"',f);
}

static void emit_root(const char *path, int rc) {
    FILE *f=g_json.fp;
    fputs("{\"record\":\"scan_root\",\"path\":",f); mm_json_cstr(f,path);
    fprintf(f,",\"accessible\":%s,\"errno\":%d}\n",rc==0?"true":"false",rc==0?0:errno);
}

static void emit_root_inventory(void) {
    DIR *d=opendir("/");
    if(!d) return;
    struct dirent *de;
    while((de=readdir(d))) {
        if(!strcmp(de->d_name,".")||!strcmp(de->d_name,"..")) continue;
        char path[PATH_MAX];
        int n=snprintf(path,sizeof(path),"/%s",de->d_name);
        if(n<0 || (size_t)n>=sizeof(path)) continue;
        struct stat st;
        if(lstat(path,&st)<0) continue;
        FILE *f=g_json.fp;
        fputs("{\"record\":\"root_entry\",\"path\":",f); mm_json_cstr(f,path);
        fprintf(f,",\"mode\":%u,\"kind\":\"%s\"",
                (unsigned)st.st_mode,
                S_ISDIR(st.st_mode)?"dir":(S_ISREG(st.st_mode)?"file":(S_ISLNK(st.st_mode)?"symlink":"other")));
        if(S_ISLNK(st.st_mode)) {
            char target[PATH_MAX]; ssize_t r=readlink(path,target,sizeof(target)-1);
            if(r>0) { target[r]=0; fputs(",\"target\":",f); mm_json_cstr(f,target); }
        }
        fputs("}\n",f);
        g_stats.root_entries++;
    }
    closedir(d);
}

static int magic_candidate(const unsigned char h[4]) {
    if (h[0]==0x7f && h[1]=='E' && h[2]=='L' && h[3]=='F') return 1;
    uint32_t be=mm_be32(h);
    return be==MM_SELF_MAGIC_PS5 || be==MM_SELF_MAGIC_PS4;
}

static int read_file_all(const char *path, unsigned char **data_out, size_t *size_out, char sha_hex[65]) {
    struct stat st;
    if (stat(path,&st)<0) return -1;
    if (st.st_size<0 || (uint64_t)st.st_size>MM_MAX_FILE_SIZE) { errno=EFBIG; return -1; }
    FILE *fp=fopen(path,"rb"); if(!fp)return -1;
    size_t sz=(size_t)st.st_size;
    unsigned char *buf=(unsigned char*)malloc(sz?sz:1);
    if(!buf){fclose(fp);errno=ENOMEM;return -1;}
    mm_sha256_ctx_t hc; mm_sha256_init(&hc);
    size_t off=0;
    while(off<sz){
        size_t want=sz-off; if(want>1024*1024)want=1024*1024;
        size_t n=fread(buf+off,1,want,fp);
        if(!n){ if(ferror(fp)){free(buf);fclose(fp);return -1;} break; }
        mm_sha256_update(&hc,buf+off,n); off+=n;
    }
    fclose(fp);
    if(off!=sz){free(buf);errno=EIO;return -1;}
    uint8_t hash[32]; mm_sha256_final(&hc,hash); mm_sha256_hex(hash,sha_hex);
    *data_out=buf; *size_out=sz; return 0;
}

static int mm_extension_candidate(const char *path);
static void emit_extension_candidate(const char *path, const struct stat *st);

static void process_file(const char *path, const struct stat *st) {
    g_stats.files_seen++;
    if (st->st_size < 4) return;
    FILE *fp=fopen(path,"rb");
    if(!fp) return;
    unsigned char h[4]; size_t n=fread(h,1,4,fp); fclose(fp);
    if(n!=4) return;
    if(!magic_candidate(h)) { if(mm_extension_candidate(path)) emit_extension_candidate(path,st); return; }
    g_stats.executable_candidates++;

    /* Preserve the raw candidate header even if the deeper ELF/SELF parser
     * rejects the image.  This keeps foreign-architecture/system-firmware
     * branches visible instead of collapsing them into a single error. */
    {
        unsigned char raw[64]={0};
        FILE *hf=fopen(path,"rb");
        size_t got=0;
        if(hf){got=fread(raw,1,sizeof(raw),hf);fclose(hf);}
        FILE *jf=g_json.fp;
        fputs("{\"record\":\"candidate_header\",\"path\":",jf); mm_json_cstr(jf,path);
        fprintf(jf,",\"file_size\":%lld,\"header_bytes\":%llu,\"header_hex\":",
                (long long)st->st_size,(unsigned long long)got);
        mm_json_hex(jf,raw,got); fputs("}\n",jf);
    }

    if((uint64_t)st->st_size > MM_MAX_FILE_SIZE) {
        FILE *lf=g_json.fp;
        fputs("{\"record\":\"large_image_candidate\",\"path\":",lf); mm_json_cstr(lf,path);
        fprintf(lf,",\"file_size\":%lld,\"capture\":\"header-only\",\"reason\":\"file exceeds bounded in-memory static parser size\"}\n",(long long)st->st_size);
        g_stats.large_image_candidates++;
        return;
    }

    printf("[MAP] %s (%lld bytes)\n",path,(long long)st->st_size);
    unsigned char *data=0; size_t size=0; char sha[65]={0};
    if(read_file_all(path,&data,&size,sha)<0){
        g_stats.parse_errors++;
        mm_emit_error(&g_json,path,"read_file",strerror(errno),errno);
        return;
    }
    char err[256]={0};
    if(mm_parse_image(path,data,size,sha,&g_json,&g_stats,err,sizeof(err))<0){
        g_stats.parse_errors++;
        mm_emit_error(&g_json,path,"parse_image",err,0);
    }
    free(data);
}

static int mm_is_own_output_tree(const char *path) {
    const char *prefix=MM_OUTDIR;
    size_t n=strlen(prefix);
    return path && !strncmp(path,prefix,n) && (path[n]==0 || path[n]=='/');
}

static int mm_extension_candidate(const char *path) {
    const char *dot=strrchr(path,'.');
    if(!dot) return 0;
    static const char *exts[]={".elf",".self",".sprx",".prx",".ebin",".bin",".so",".o",".a",0};
    for(int i=0;exts[i];i++) if(!strcasecmp(dot,exts[i])) return 1;
    return 0;
}

static void emit_extension_candidate(const char *path, const struct stat *st) {
    FILE *f=g_json.fp;
    fputs("{\"record\":\"extension_candidate\",\"path\":",f); mm_json_cstr(f,path);
    fprintf(f,",\"file_size\":%lld,\"reason\":\"API-bearing extension but ELF/SELF magic not recognized\"}\n",(long long)st->st_size);
    g_stats.extension_candidates++;
}

static void scan_tree(const char *root, int depth) {
    if(depth>MM_MAX_DEPTH) return;
    DIR *d=opendir(root);
    if(!d) return;
    g_stats.dirs_seen++;
    struct dirent *de;
    while((de=readdir(d))){
        if(!strcmp(de->d_name,".")||!strcmp(de->d_name,".."))continue;
        char path[PATH_MAX];
        int m=snprintf(path,sizeof(path),"%s/%s",root,de->d_name);
        if(m<0 || (size_t)m>=sizeof(path))continue;
        struct stat st;
        if(lstat(path,&st)<0)continue;
        if(mm_is_own_output_tree(path)) {
            FILE *f=g_json.fp;
            fputs("{\"record\":\"filesystem_skip\",\"path\":",f); mm_json_cstr(f,path);
            fputs(",\"reason\":\"mapper output tree excluded to prevent self-recursion\"}\n",f);
            continue;
        }
        if(S_ISLNK(st.st_mode)) {
            FILE *f=g_json.fp;
            fputs("{\"record\":\"filesystem_symlink\",\"path\":",f); mm_json_cstr(f,path);
            char target[PATH_MAX]; ssize_t r=readlink(path,target,sizeof(target)-1);
            if(r>0) { target[r]=0; fputs(",\"target\":",f); mm_json_cstr(f,target); }
            fputs("}\n",f);
            continue;
        }
        if(S_ISDIR(st.st_mode)) {
            FILE *f=g_json.fp;
            fputs("{\"record\":\"filesystem_dir\",\"path\":",f); mm_json_cstr(f,path);
            fprintf(f,",\"mode\":%u,\"dev\":%llu,\"ino\":%llu}\n",(unsigned)st.st_mode,(unsigned long long)st.st_dev,(unsigned long long)st.st_ino);
            g_stats.filesystem_dirs_emitted++;
            scan_tree(path,depth+1);
        } else if(S_ISREG(st.st_mode)) process_file(path,&st);
    }
    closedir(d);
}

static void scan_devices_tree(const char *root, int depth) {
    if(depth>MM_MAX_DEVICE_DEPTH) return;
    DIR *d=opendir(root); if(!d)return;
    struct dirent *de;
    while((de=readdir(d))){
        if(!strcmp(de->d_name,".")||!strcmp(de->d_name,".."))continue;
        char path[PATH_MAX];
        int n=snprintf(path,sizeof(path),"%s/%s",root,de->d_name);
        if(n<0 || (size_t)n>=sizeof(path))continue;
        struct stat st; if(lstat(path,&st)<0)continue;
        FILE *f=g_json.fp;
        fputs("{\"record\":\"device_node\",\"path\":",f); mm_json_cstr(f,path);
        fprintf(f,",\"mode\":%u,\"rdev\":%llu,\"kind\":\"%s\"",
                (unsigned)st.st_mode,(unsigned long long)st.st_rdev,
                S_ISCHR(st.st_mode)?"char":(S_ISBLK(st.st_mode)?"block":(S_ISDIR(st.st_mode)?"dir":(S_ISLNK(st.st_mode)?"symlink":"other"))));
        if(S_ISLNK(st.st_mode)) {
            char target[PATH_MAX]; ssize_t r=readlink(path,target,sizeof(target)-1);
            if(r>0){target[r]=0;fputs(",\"target\":",f);mm_json_cstr(f,target);}
        }
        fputs("}\n",f);
        g_stats.devices++;
        g_stats.device_entries++;
        if(S_ISDIR(st.st_mode)) scan_devices_tree(path,depth+1);
    }
    closedir(d);
}

static int mm_external_root_dir(const char *name) {
    /* Host/USB roots are external media/PC bridges rather than PS5-internal
     * API storage.  They are inventoried but not recursively walked by default
     * to avoid mapping arbitrary external files as console firmware. */
    static const char *external[]={"host","host0","hostapp","usb",0};
    for(int i=0;external[i];i++) if(!strcmp(name,external[i])) return 1;
    return 0;
}

static void scan_maximum_observable_roots(void) {
    DIR *d=opendir("/");
    if(!d) return;
    struct dirent *de;
    while((de=readdir(d))) {
        if(!strcmp(de->d_name,".")||!strcmp(de->d_name,"..")) continue;
        char path[PATH_MAX];
        int n=snprintf(path,sizeof(path),"/%s",de->d_name);
        if(n<0 || (size_t)n>=sizeof(path)) continue;
        struct stat st; if(lstat(path,&st)<0) continue;

        if(S_ISREG(st.st_mode)) {
            process_file(path,&st);
            continue;
        }
        if(!S_ISDIR(st.st_mode)) continue;

        g_stats.roots_seen++;
        FILE *f=g_json.fp;
        if(!strcmp(de->d_name,"dev")) {
            fputs("{\"record\":\"scan_root_policy\",\"path\":",f); mm_json_cstr(f,path);
            fputs(",\"selected\":false,\"reason\":\"handled by dedicated /dev branch\"}\n",f);
            continue;
        }
        if(mm_external_root_dir(de->d_name)) {
            fputs("{\"record\":\"scan_root_policy\",\"path\":",f); mm_json_cstr(f,path);
            fputs(",\"selected\":false,\"reason\":\"external host/removable-media tree; inventoried but not PS5-internal firmware\"}\n",f);
            continue;
        }

        DIR *probe=opendir(path);
        if(!probe) { emit_root(path,-1); continue; }
        closedir(probe);
        g_stats.roots_opened++; emit_root(path,0);
        fputs("{\"record\":\"scan_root_policy\",\"path\":",f); mm_json_cstr(f,path);
        fputs(",\"selected\":true,\"reason\":\"maximum PS5-internal accessible-root census\"}\n",f);
        printf("[ROOT] %s\n",path);
        scan_tree(path,0);
    }
    closedir(d);
}

static void write_summary(const char *path, uint32_t fw, const char *map_path) {
    FILE *f=fopen(path,"w"); if(!f)return;
    fprintf(f,"MM PS5 API MAPPER %s\n",MM_VERSION);
    fprintf(f,"Firmware raw: 0x%08x\n",fw);
    fprintf(f,"Map: %s\n\n",map_path);
    fprintf(f,"STATIC FILESYSTEM BRANCH\n");
    fprintf(f,"roots_seen=%llu roots_opened=%llu root_entries=%llu dirs=%llu files=%llu candidates=%llu\n",
            (unsigned long long)g_stats.roots_seen,(unsigned long long)g_stats.roots_opened,
            (unsigned long long)g_stats.root_entries,(unsigned long long)g_stats.dirs_seen,
            (unsigned long long)g_stats.files_seen,(unsigned long long)g_stats.executable_candidates);
    fprintf(f,"images=%llu raw_elf=%llu self=%llu parse_errors=%llu\n",
            (unsigned long long)g_stats.images_parsed,(unsigned long long)g_stats.raw_elf,
            (unsigned long long)g_stats.self_images,(unsigned long long)g_stats.parse_errors);
    fprintf(f,"self_encrypted_segments=%llu self_compressed_segments=%llu\n",
            (unsigned long long)g_stats.self_encrypted_segments,(unsigned long long)g_stats.self_compressed_segments);
    fprintf(f,"static_dynamic=%llu static_symbols=%llu static_imports=%llu static_exports=%llu static_relocations=%llu static_dependencies=%llu\n\n",
            (unsigned long long)g_stats.dynamic_entries,(unsigned long long)g_stats.symbols,
            (unsigned long long)g_stats.imports,(unsigned long long)g_stats.exports,
            (unsigned long long)g_stats.relocations,(unsigned long long)g_stats.dependencies);

    fprintf(f,"RUNTIME ALLPROC/DYNLIB BRANCH\n");
    fprintf(f,"runtime_processes=%llu runtime_modules=%llu runtime_dynsecs=%llu runtime_dynamic=%llu\n",
            (unsigned long long)g_stats.runtime_processes,(unsigned long long)g_stats.runtime_modules,
            (unsigned long long)g_stats.runtime_dynsecs,(unsigned long long)g_stats.runtime_dynamic_entries);
    fprintf(f,"runtime_symbols=%llu runtime_imports=%llu runtime_exports=%llu runtime_relocations=%llu runtime_dependencies=%llu\n",
            (unsigned long long)g_stats.runtime_symbols,(unsigned long long)g_stats.runtime_imports,
            (unsigned long long)g_stats.runtime_exports,(unsigned long long)g_stats.runtime_relocations,
            (unsigned long long)g_stats.runtime_dependencies);
    fprintf(f,"runtime_errors=%llu runtime_bytes_copied=%llu runtime_strtab_strings=%llu\n",
            (unsigned long long)g_stats.runtime_errors,(unsigned long long)g_stats.runtime_bytes_copied,
            (unsigned long long)g_stats.runtime_strtab_strings);
    fprintf(f,"runtime_raw_blobs=%llu runtime_raw_chunks=%llu runtime_raw_bytes=%llu runtime_library_attrs=%llu\n",
            (unsigned long long)g_stats.runtime_raw_blobs,(unsigned long long)g_stats.runtime_raw_chunks,
            (unsigned long long)g_stats.runtime_raw_bytes,(unsigned long long)g_stats.runtime_library_attrs);
    fprintf(f,"runtime_process_resources=%llu runtime_threads=%llu runtime_vmspaces=%llu runtime_vm_regions=%llu runtime_resource_anchors=%llu runtime_system_anchors=%llu\n",
            (unsigned long long)g_stats.runtime_process_resources,(unsigned long long)g_stats.runtime_threads,
            (unsigned long long)g_stats.runtime_vmspaces,(unsigned long long)g_stats.runtime_vm_regions,
            (unsigned long long)g_stats.runtime_resource_anchors,(unsigned long long)g_stats.runtime_system_anchors);
    fprintf(f,"runtime_fd_tables=%llu runtime_fds=%llu runtime_fd_data_raw=%llu runtime_fd_data_raw_bytes=%llu runtime_fd_data_pointer_candidates=%llu runtime_bus_devices=%llu runtime_bus_nodes_walked=%llu runtime_bus_raw_candidates=%llu runtime_bus_candidate_strings=%llu runtime_bus_list_terminators=%llu runtime_bus_snapshot_stable=%llu runtime_bus_drivers=%llu runtime_bus_driver_methods=%llu runtime_bus_softc_heads=%llu runtime_bus_softc_bytes=%llu runtime_observable_limits=%llu\n\n",
            (unsigned long long)g_stats.runtime_fd_tables,(unsigned long long)g_stats.runtime_fds,
            (unsigned long long)g_stats.runtime_fd_data_raw,(unsigned long long)g_stats.runtime_fd_data_raw_bytes,
            (unsigned long long)g_stats.runtime_fd_data_pointer_candidates,(unsigned long long)g_stats.runtime_bus_devices,(unsigned long long)g_stats.runtime_bus_nodes_walked,
            (unsigned long long)g_stats.runtime_bus_raw_candidates,(unsigned long long)g_stats.runtime_bus_candidate_strings,
            (unsigned long long)g_stats.runtime_bus_list_terminators,(unsigned long long)g_stats.runtime_bus_snapshot_stable,
            (unsigned long long)g_stats.runtime_bus_drivers,(unsigned long long)g_stats.runtime_bus_driver_methods,
            (unsigned long long)g_stats.runtime_bus_softc_heads,(unsigned long long)g_stats.runtime_bus_softc_bytes,
            (unsigned long long)g_stats.runtime_observable_limits);

    fprintf(f,"DEVICE/ROOT INVENTORY BRANCH\n");
    fprintf(f,"device_entries=%llu devices=%llu filesystem_dirs=%llu extension_candidates=%llu large_image_candidates=%llu\n\n",
            (unsigned long long)g_stats.device_entries,(unsigned long long)g_stats.devices,
            (unsigned long long)g_stats.filesystem_dirs_emitted,(unsigned long long)g_stats.extension_candidates,(unsigned long long)g_stats.large_image_candidates);

    fprintf(f,"TOTAL SYMBOL SURFACE=%llu\n",
            (unsigned long long)(g_stats.symbols+g_stats.runtime_symbols));
    fprintf(f,"TOTAL IMPORT SURFACE=%llu\n",
            (unsigned long long)(g_stats.imports+g_stats.runtime_imports));
    fprintf(f,"TOTAL EXPORT SURFACE=%llu\n",
            (unsigned long long)(g_stats.exports+g_stats.runtime_exports));
    fprintf(f,"NOTE: SDK/NID name enrichment is generated on the PC and merged by RESOLVE-MAP-WINDOWS.bat.\n");
    fclose(f);
}

int main(void) {
    memset(&g_stats,0,sizeof(g_stats));
    mkdir(MM_OUTDIR,0777);
    uint32_t fw=0;
#if MM_HAVE_PS5_KERNEL
    fw=kernel_get_fw_version();
#endif
    time_t now=time(NULL);
    char map_path[PATH_MAX],sum_path[PATH_MAX],index_path[PATH_MAX];
    snprintf(map_path,sizeof(map_path),MM_OUTDIR "/full_map_fw_%08x_%lld.jsonl",fw,(long long)now);
    snprintf(sum_path,sizeof(sum_path),MM_OUTDIR "/full_summary_fw_%08x_%lld.txt",fw,(long long)now);
    snprintf(index_path,sizeof(index_path),MM_OUTDIR "/LATEST.txt");
    g_json.fp=fopen(map_path,"w");
    if(!g_json.fp){printf("[FAIL] cannot create %s: %s\n",map_path,strerror(errno));return 1;}

    struct utsname u; memset(&u,0,sizeof(u)); uname(&u);
    FILE *f=g_json.fp;
    fputs("{\"record\":\"header\",\"tool\":\"MM-PS5-API-MAPPER\",\"version\":",f); mm_json_cstr(f,MM_VERSION);
    fprintf(f,",\"firmware_raw\":\"0x%08x\",\"epoch\":%lld,\"sysname\":",fw,(long long)now); mm_json_cstr(f,u.sysname);
    fputs(",\"release\":",f);mm_json_cstr(f,u.release); fputs(",\"machine\":",f);mm_json_cstr(f,u.machine);
    fputs(",\"policy\":\"read-only resource/device correlation graph with runtime process/module/VM/FD capture, bounded raw resource evidence, bus/device/driver mapping and PC-side exact pointer correlations; credentials excluded; arbitrary raw pointers are not dereferenced; no module loading, no discovered API execution, no patching\"}\n",f);

    printf("====================================================================\n");
    printf(" MM PS5 API MAPPER %s\n",MM_VERSION);
    printf(" RESOURCE GRAPH: ROOTS + /DEV + ALLPROC/DYNLIB + THREADS + VM + FD RESOURCE RAW + COMPLETE BUS TAILQ\n");
    printf(" READ ONLY: no module loading, no discovered API execution, no patching\n");
    printf(" Output: %s\n",map_path);
    printf("====================================================================\n");

    printf("\n[BRANCH 1] Root inventory...\n");
    emit_root_inventory();

    printf("\n[BRANCH 2] Maximum PS5-internal filesystem ELF/SELF census...\n");
    scan_maximum_observable_roots();

    printf("\n[BRANCH 3] Full /dev inventory...\n");
    scan_devices_tree("/dev",0);

    printf("\n[BRANCH 4] Runtime process/FD-resource/bus-TAILQ/driver/dynlib census...\n");
    int runtime_rc=mm_runtime_scan_all_processes(&g_json,&g_stats);
    printf("[RUNTIME] rc=%d processes=%llu threads=%llu vm_regions=%llu modules=%llu dynsecs=%llu symbols=%llu imports=%llu exports=%llu relocs=%llu errors=%llu\n",
           runtime_rc,
           (unsigned long long)g_stats.runtime_processes,(unsigned long long)g_stats.runtime_threads,
           (unsigned long long)g_stats.runtime_vm_regions,(unsigned long long)g_stats.runtime_modules,
           (unsigned long long)g_stats.runtime_dynsecs,(unsigned long long)g_stats.runtime_symbols,
           (unsigned long long)g_stats.runtime_imports,(unsigned long long)g_stats.runtime_exports,
           (unsigned long long)g_stats.runtime_relocations,(unsigned long long)g_stats.runtime_errors);

    fputs("{\"record\":\"summary\"",f);
#define S(name) fprintf(f,",\"" #name "\":%llu",(unsigned long long)g_stats.name)
    S(roots_seen);S(roots_opened);S(root_entries);S(dirs_seen);S(files_seen);S(executable_candidates);S(images_parsed);S(raw_elf);S(self_images);
    S(self_encrypted_segments);S(self_compressed_segments);S(parse_errors);S(dynamic_entries);S(symbols);S(imports);S(exports);S(relocations);S(dependencies);
    S(devices);S(device_entries);S(filesystem_dirs_emitted);S(extension_candidates);S(large_image_candidates);S(runtime_processes);S(runtime_modules);S(runtime_dynsecs);S(runtime_dynamic_entries);S(runtime_symbols);S(runtime_imports);
    S(runtime_exports);S(runtime_relocations);S(runtime_dependencies);S(runtime_errors);S(runtime_bytes_copied);S(runtime_strtab_strings);S(runtime_raw_blobs);S(runtime_raw_chunks);S(runtime_raw_bytes);S(runtime_library_attrs);
    S(runtime_process_resources);S(runtime_threads);S(runtime_vmspaces);S(runtime_vm_regions);S(runtime_resource_anchors);S(runtime_system_anchors);
    S(runtime_fd_tables);S(runtime_fds);S(runtime_fd_data_raw);S(runtime_fd_data_raw_bytes);S(runtime_fd_data_pointer_candidates);S(runtime_bus_devices);S(runtime_bus_nodes_walked);S(runtime_bus_raw_candidates);S(runtime_bus_candidate_strings);S(runtime_bus_list_terminators);S(runtime_bus_snapshot_stable);S(runtime_bus_drivers);S(runtime_bus_driver_methods);S(runtime_bus_softc_heads);S(runtime_bus_softc_bytes);S(runtime_observable_limits);
#undef S
    fputs("}\n",f); fflush(f); fclose(f);
    write_summary(sum_path,fw,map_path);
    FILE *idx=fopen(index_path,"w"); if(idx){fprintf(idx,"MAP=%s\nSUMMARY=%s\n",map_path,sum_path);fclose(idx);}

    printf("\n[PASS] RESOURCE DEVICE CORRELATION CAPTURE COMPLETE\n");
    printf("[MAP] %s\n[SUMMARY] %s\n",map_path,sum_path);
    printf("[STATIC] images=%llu symbols=%llu imports=%llu exports=%llu relocs=%llu gaps/errors=%llu\n",
           (unsigned long long)g_stats.images_parsed,(unsigned long long)g_stats.symbols,
           (unsigned long long)g_stats.imports,(unsigned long long)g_stats.exports,
           (unsigned long long)g_stats.relocations,(unsigned long long)g_stats.parse_errors);
    printf("[RUNTIME] processes=%llu threads=%llu vm_regions=%llu modules=%llu symbols=%llu imports=%llu exports=%llu relocs=%llu runtime_errors=%llu\n",
           (unsigned long long)g_stats.runtime_processes,(unsigned long long)g_stats.runtime_threads,
           (unsigned long long)g_stats.runtime_vm_regions,(unsigned long long)g_stats.runtime_modules,
           (unsigned long long)g_stats.runtime_symbols,(unsigned long long)g_stats.runtime_imports,
           (unsigned long long)g_stats.runtime_exports,(unsigned long long)g_stats.runtime_relocations,
           (unsigned long long)g_stats.runtime_errors);
    printf("[HARDWARE-GRAPH] fd_tables=%llu fds=%llu fd_data_raw=%llu fd_data_ptrs=%llu bus_decoded=%llu bus_nodes=%llu bus_raw=%llu bus_strings=%llu bus_terminators=%llu bus_snapshot_stable=%llu drivers=%llu driver_methods=%llu softc_heads=%llu observable_limits=%llu\n",
           (unsigned long long)g_stats.runtime_fd_tables,(unsigned long long)g_stats.runtime_fds,
           (unsigned long long)g_stats.runtime_fd_data_raw,(unsigned long long)g_stats.runtime_fd_data_pointer_candidates,
           (unsigned long long)g_stats.runtime_bus_devices,(unsigned long long)g_stats.runtime_bus_nodes_walked,
           (unsigned long long)g_stats.runtime_bus_raw_candidates,(unsigned long long)g_stats.runtime_bus_candidate_strings,
           (unsigned long long)g_stats.runtime_bus_list_terminators,(unsigned long long)g_stats.runtime_bus_snapshot_stable,
           (unsigned long long)g_stats.runtime_bus_drivers,(unsigned long long)g_stats.runtime_bus_driver_methods,
           (unsigned long long)g_stats.runtime_bus_softc_heads,(unsigned long long)g_stats.runtime_observable_limits);
    printf("[TOTAL] symbols=%llu imports=%llu exports=%llu device_entries=%llu\n",
           (unsigned long long)(g_stats.symbols+g_stats.runtime_symbols),
           (unsigned long long)(g_stats.imports+g_stats.runtime_imports),
           (unsigned long long)(g_stats.exports+g_stats.runtime_exports),
           (unsigned long long)g_stats.device_entries);
    return 0;
}

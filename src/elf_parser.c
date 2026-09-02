#include "mm_elf.h"
#include "mm_mapper.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>

#define MM_MAX_PHNUM 1024u
#define MM_MAX_DYN_ENTRIES 65536u
#define MM_MAX_SYMBOLS 1000000u
#define MM_MAX_RELOCS 2000000u

typedef struct mm_img_ctx {
    const unsigned char *data;
    size_t size;
    size_t elf_base;
    int is_self;
    uint16_t self_num_segments;
    mm_elf64_ehdr_t eh;
} mm_img_ctx_t;

static int range_ok(size_t size, uint64_t off, uint64_t len) {
    return off <= size && len <= size - (size_t)off;
}

static size_t bounded_cstr_len(const unsigned char *p, size_t max) {
    size_t i=0;
    while (i<max && p[i]) ++i;
    return i;
}

static int read_phdr(const mm_img_ctx_t *c, uint16_t idx, mm_elf64_phdr_t *out) {
    if (idx >= c->eh.e_phnum || c->eh.e_phentsize < sizeof(*out)) return -1;
    uint64_t off=(uint64_t)c->elf_base + c->eh.e_phoff + (uint64_t)idx*c->eh.e_phentsize;
    if (!range_ok(c->size,off,sizeof(*out))) return -1;
    memcpy(out,c->data+off,sizeof(*out));
    return 0;
}

static int read_self_seg(const mm_img_ctx_t *c, uint16_t idx, mm_self_segment_t *out) {
    if (!c->is_self || idx >= c->self_num_segments) return -1;
    uint64_t off=32u + (uint64_t)idx*32u;
    if (!range_ok(c->size,off,sizeof(*out))) return -1;
    memcpy(out,c->data+off,sizeof(*out));
    return 0;
}

static int self_segment_for_phdr(const mm_img_ctx_t *c, uint16_t phidx, mm_self_segment_t *out) {
    for (uint16_t i=0;i<c->self_num_segments;i++) {
        mm_self_segment_t s;
        if (read_self_seg(c,i,&s)) continue;
        if (!(s.flags & MM_SELF_SEGMENT_FLAG_DATA)) continue;
        uint16_t key=(uint16_t)((s.flags >> 20) & 0x0fff);
        if (key == phidx) { *out=s; return 0; }
    }
    return -1;
}

static int vaddr_to_fileoff(const mm_img_ctx_t *c, uint64_t va, uint64_t *out) {
    for (uint16_t i=0;i<c->eh.e_phnum;i++) {
        mm_elf64_phdr_t ph;
        if (read_phdr(c,i,&ph)) continue;
        if (ph.p_type != MM_PT_LOAD || ph.p_filesz == 0) continue;
        if (va < ph.p_vaddr || va >= ph.p_vaddr + ph.p_filesz) continue;
        uint64_t delta=va-ph.p_vaddr;
        uint64_t off;
        if (c->is_self) {
            mm_self_segment_t s;
            if (self_segment_for_phdr(c,i,&s)) continue;
            off=s.file_offset+delta;
        } else {
            off=(uint64_t)c->elf_base+ph.p_offset+delta;
        }
        if (!range_ok(c->size,off,1)) return -1;
        *out=off;
        return 0;
    }
    /* Some small raw ELFs use absolute file-like dynamic offsets. */
    if (!c->is_self && range_ok(c->size,(uint64_t)c->elf_base+va,1)) {
        *out=(uint64_t)c->elf_base+va;
        return 0;
    }
    return -1;
}

static int self_flags_for_vaddr(const mm_img_ctx_t *c, uint64_t va, uint64_t *flags_out) {
    if (!c->is_self) return -1;
    for (uint16_t i=0;i<c->eh.e_phnum;i++) {
        mm_elf64_phdr_t ph;
        if (read_phdr(c,i,&ph)) continue;
        if (ph.p_type != MM_PT_LOAD || ph.p_filesz == 0) continue;
        if (va < ph.p_vaddr || va >= ph.p_vaddr + ph.p_filesz) continue;
        mm_self_segment_t ss;
        if (self_segment_for_phdr(c,i,&ss)) return -1;
        *flags_out=ss.flags;
        return 0;
    }
    return -1;
}

static const char *ph_type_name(uint32_t t) {
    switch(t) {
        case MM_PT_LOAD:return "PT_LOAD"; case MM_PT_DYNAMIC:return "PT_DYNAMIC";
        case MM_PT_INTERP:return "PT_INTERP"; case MM_PT_NOTE:return "PT_NOTE";
        case MM_PT_SHLIB:return "PT_SHLIB"; case MM_PT_PHDR:return "PT_PHDR";
        case MM_PT_TLS:return "PT_TLS"; case MM_PT_SCE_DYNLIBDATA:return "PT_SCE_DYNLIBDATA";
        case MM_PT_SCE_PROCPARAM:return "PT_SCE_PROCPARAM"; case MM_PT_SCE_MODULEPARAM:return "PT_SCE_MODULEPARAM";
        case MM_PT_SCE_RELRO:return "PT_SCE_RELRO"; case MM_PT_GNU_EH_FRAME:return "PT_GNU_EH_FRAME";
        case MM_PT_GNU_STACK:return "PT_GNU_STACK"; case MM_PT_GNU_RELRO:return "PT_GNU_RELRO";
        case MM_PT_SCE_COMMENT:return "PT_SCE_COMMENT"; case MM_PT_SCE_LIBVERSION:return "PT_SCE_LIBVERSION";
        default:return "UNKNOWN";
    }
}

static const char *dyn_tag_name(int64_t t) {
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

static int string_at(const mm_img_ctx_t *c, uint64_t strtab_off, uint64_t strtab_size,
                     uint64_t nameoff, const unsigned char **p, size_t *len) {
    if (nameoff >= strtab_size) return -1;
    uint64_t off=strtab_off+nameoff;
    uint64_t remain=strtab_size-nameoff;
    if (!range_ok(c->size,off,remain)) {
        if (!range_ok(c->size,off,1)) return -1;
        remain=c->size-(size_t)off;
    }
    *p=c->data+off;
    *len=bounded_cstr_len(*p,(size_t)remain);
    return 0;
}

static void emit_named_dyn(mm_jsonl_t *out, const char *path, const char *kind,
                           uint64_t value, const unsigned char *name, size_t name_len) {
    FILE *f=out->fp;
    uint16_t id=(uint16_t)(value>>48);
    uint8_t major=(uint8_t)(value>>32);
    uint8_t minor=(uint8_t)(value>>40);
    fputs("{\"record\":\"library_ref\",\"path\":",f); mm_json_cstr(f,path);
    fputs(",\"kind\":",f); mm_json_cstr(f,kind);
    fprintf(f,",\"object_id\":%u,\"version_major\":%u,\"version_minor\":%u,\"name\":",
            (unsigned)id,(unsigned)major,(unsigned)minor);
    mm_json_string(f,name,name_len);
    fprintf(f,",\"raw\":\"0x%016llx\"}\n",(unsigned long long)value);
}

static int emit_reloc_table(const mm_img_ctx_t *c, const char *path, mm_jsonl_t *out, mm_stats_t *stats,
                            uint64_t rel_va, uint64_t rel_size, const char *table,
                            uint64_t symtab_off, uint64_t syment, uint64_t symcount,
                            uint64_t strtab_off, uint64_t strtab_size) {
    if (!rel_va || rel_size < sizeof(mm_elf64_rela_t)) return 0;
    uint64_t rel_off;
    if (vaddr_to_fileoff(c,rel_va,&rel_off)) return -1;
    uint64_t count=rel_size/sizeof(mm_elf64_rela_t);
    if (count>MM_MAX_RELOCS) count=MM_MAX_RELOCS;
    for (uint64_t i=0;i<count;i++) {
        uint64_t off=rel_off+i*sizeof(mm_elf64_rela_t);
        if (!range_ok(c->size,off,sizeof(mm_elf64_rela_t))) break;
        mm_elf64_rela_t r; memcpy(&r,c->data+off,sizeof(r));
        uint32_t si=mm_elf64_r_sym(r.r_info);
        FILE *f=out->fp;
        fputs("{\"record\":\"relocation\",\"path\":",f); mm_json_cstr(f,path);
        fputs(",\"table\":",f); mm_json_cstr(f,table);
        fprintf(f,",\"index\":%llu,\"offset\":\"0x%016llx\",\"type\":%u,\"symbol_index\":%u,\"addend\":%lld",
                (unsigned long long)i,(unsigned long long)r.r_offset,mm_elf64_r_type(r.r_info),si,(long long)r.r_addend);
        if (si<symcount && syment>=sizeof(mm_elf64_sym_t)) {
            uint64_t so=symtab_off+(uint64_t)si*syment;
            if (range_ok(c->size,so,sizeof(mm_elf64_sym_t))) {
                mm_elf64_sym_t s; memcpy(&s,c->data+so,sizeof(s));
                const unsigned char *sp; size_t sl;
                if (!string_at(c,strtab_off,strtab_size,s.st_name,&sp,&sl)) {
                    fputs(",\"symbol\":",f); mm_json_string(f,sp,sl);
                }
            }
        }
        fputs("}\n",f);
        stats->relocations++;
    }
    return 0;
}

int mm_parse_image(const char *path, const unsigned char *data, size_t size,
                   const char *sha256_hex, mm_jsonl_t *out, mm_stats_t *stats,
                   char *errbuf, size_t errbuf_len) {
    mm_img_ctx_t c; memset(&c,0,sizeof(c)); c.data=data; c.size=size;
    if (size < sizeof(mm_elf64_ehdr_t)) { snprintf(errbuf,errbuf_len,"file too small"); return -1; }

    if (data[0]==MM_ELF_MAGIC_0 && data[1]==MM_ELF_MAGIC_1 && data[2]==MM_ELF_MAGIC_2 && data[3]==MM_ELF_MAGIC_3) {
        c.elf_base=0; c.is_self=0; stats->raw_elf++;
    } else {
        uint32_t magic=mm_be32(data);
        if (magic!=MM_SELF_MAGIC_PS5 && magic!=MM_SELF_MAGIC_PS4) { snprintf(errbuf,errbuf_len,"not ELF/SELF"); return -1; }
        if (size<32) { snprintf(errbuf,errbuf_len,"truncated SELF header"); return -1; }
        uint16_t nseg=(uint16_t)(data[24] | ((uint16_t)data[25]<<8));
        uint64_t ebase=32u+(uint64_t)nseg*32u;
        if (!range_ok(size,ebase,sizeof(mm_elf64_ehdr_t))) { snprintf(errbuf,errbuf_len,"truncated SELF segment table"); return -1; }
        if (data[ebase]!=MM_ELF_MAGIC_0 || data[ebase+1]!=MM_ELF_MAGIC_1 || data[ebase+2]!=MM_ELF_MAGIC_2 || data[ebase+3]!=MM_ELF_MAGIC_3) {
            snprintf(errbuf,errbuf_len,"SELF has no embedded ELF at expected offset"); return -1;
        }
        c.is_self=1; c.self_num_segments=nseg; c.elf_base=(size_t)ebase; stats->self_images++;
        for (uint16_t i=0;i<nseg;i++) {
            mm_self_segment_t s; if (read_self_seg(&c,i,&s)) break;
            if (s.flags & MM_SELF_SEGMENT_FLAG_ENCRYPTED) stats->self_encrypted_segments++;
            if (s.flags & MM_SELF_SEGMENT_FLAG_COMPRESSED) stats->self_compressed_segments++;
        }
    }

    memcpy(&c.eh,data+c.elf_base,sizeof(c.eh));
    if (c.eh.e_ident[4]!=2 || c.eh.e_ident[5]!=1 || c.eh.e_machine!=MM_EM_X86_64) {
        snprintf(errbuf,errbuf_len,"unsupported ELF class/endian/machine"); return -1;
    }
    if (c.eh.e_phnum>MM_MAX_PHNUM || c.eh.e_phentsize<sizeof(mm_elf64_phdr_t)) {
        snprintf(errbuf,errbuf_len,"invalid program-header table"); return -1;
    }
    uint64_t phbytes=(uint64_t)c.eh.e_phnum*c.eh.e_phentsize;
    if (!range_ok(size,(uint64_t)c.elf_base+c.eh.e_phoff,phbytes)) {
        snprintf(errbuf,errbuf_len,"program-header table out of range"); return -1;
    }

    FILE *f=out->fp;
    fputs("{\"record\":\"image\",\"path\":",f); mm_json_cstr(f,path);
    fputs(",\"sha256\":",f); mm_json_cstr(f,sha256_hex);
    fprintf(f,",\"container\":\"%s\",\"elf_base\":%llu,\"elf_type\":%u,\"machine\":%u,\"entry\":\"0x%016llx\",\"phnum\":%u,\"shnum\":%u",
            c.is_self?"SELF":"ELF",(unsigned long long)c.elf_base,(unsigned)c.eh.e_type,(unsigned)c.eh.e_machine,
            (unsigned long long)c.eh.e_entry,(unsigned)c.eh.e_phnum,(unsigned)c.eh.e_shnum);
    if (c.is_self) fprintf(f,",\"self_segments\":%u",(unsigned)c.self_num_segments);
    fputs("}\n",f);

    uint64_t dyn_va=0,dyn_size=0;
    for (uint16_t i=0;i<c.eh.e_phnum;i++) {
        mm_elf64_phdr_t ph; if (read_phdr(&c,i,&ph)) continue;
        fputs("{\"record\":\"segment\",\"path\":",f); mm_json_cstr(f,path);
        fprintf(f,",\"index\":%u,\"type\":%u,\"type_name\":",(unsigned)i,ph.p_type); mm_json_cstr(f,ph_type_name(ph.p_type));
        fprintf(f,",\"flags\":%u,\"offset\":\"0x%llx\",\"vaddr\":\"0x%016llx\",\"filesz\":%llu,\"memsz\":%llu,\"align\":%llu",
                ph.p_flags,(unsigned long long)ph.p_offset,(unsigned long long)ph.p_vaddr,
                (unsigned long long)ph.p_filesz,(unsigned long long)ph.p_memsz,(unsigned long long)ph.p_align);
        if (c.is_self && ph.p_type==MM_PT_LOAD) {
            mm_self_segment_t ss;
            if (!self_segment_for_phdr(&c,i,&ss)) {
                fprintf(f,",\"self_file_offset\":\"0x%llx\",\"self_flags\":\"0x%llx\",\"encrypted\":%s,\"compressed\":%s",
                        (unsigned long long)ss.file_offset,(unsigned long long)ss.flags,
                        (ss.flags&MM_SELF_SEGMENT_FLAG_ENCRYPTED)?"true":"false",
                        (ss.flags&MM_SELF_SEGMENT_FLAG_COMPRESSED)?"true":"false");
            }
        }
        fputs("}\n",f);
        if (ph.p_type==MM_PT_DYNAMIC) { dyn_va=ph.p_vaddr; dyn_size=ph.p_filesz; }
    }

    if (!dyn_va || dyn_size<sizeof(mm_elf64_dyn_t)) { stats->images_parsed++; return 0; }
    if (c.is_self) {
        uint64_t backing_flags=0;
        if (!self_flags_for_vaddr(&c,dyn_va,&backing_flags) &&
            (backing_flags & (MM_SELF_SEGMENT_FLAG_ENCRYPTED|MM_SELF_SEGMENT_FLAG_COMPRESSED))) {
            fputs("{\"record\":\"coverage_gap\",\"path\":",f); mm_json_cstr(f,path);
            fprintf(f,",\"stage\":\"dynamic\",\"reason\":\"SELF backing segment is %s%s; raw file bytes are not treated as decoded API metadata\",\"self_flags\":\"0x%llx\"}\n",
                    (backing_flags&MM_SELF_SEGMENT_FLAG_ENCRYPTED)?"encrypted":"",
                    (backing_flags&MM_SELF_SEGMENT_FLAG_COMPRESSED)?((backing_flags&MM_SELF_SEGMENT_FLAG_ENCRYPTED)?"+compressed":"compressed"):"",
                    (unsigned long long)backing_flags);
            stats->images_parsed++;
            return 0;
        }
    }
    uint64_t dyn_off;
    if (vaddr_to_fileoff(&c,dyn_va,&dyn_off)) { snprintf(errbuf,errbuf_len,"cannot map PT_DYNAMIC to file"); return -1; }
    uint64_t dcount=dyn_size/sizeof(mm_elf64_dyn_t); if (dcount>MM_MAX_DYN_ENTRIES) dcount=MM_MAX_DYN_ENTRIES;

    uint64_t str_va=0,str_sz=0,sym_va=0,sym_sz=0,syment=sizeof(mm_elf64_sym_t);
    uint64_t rela_va=0,rela_sz=0,jmp_va=0,jmp_sz=0;
    for (uint64_t i=0;i<dcount;i++) {
        uint64_t off=dyn_off+i*sizeof(mm_elf64_dyn_t); if (!range_ok(size,off,sizeof(mm_elf64_dyn_t))) break;
        mm_elf64_dyn_t d; memcpy(&d,data+off,sizeof(d));
        if (d.d_tag==MM_DT_NULL) break;
        switch(d.d_tag) {
            case MM_DT_STRTAB: case MM_DT_SCE_STRTAB: if(!str_va)str_va=d.d_val; break;
            case MM_DT_STRSZ: case MM_DT_SCE_STRSIZE: if(!str_sz)str_sz=d.d_val; break;
            case MM_DT_SYMTAB: case MM_DT_SCE_SYMTAB: if(!sym_va)sym_va=d.d_val; break;
            case MM_DT_SCE_SYMTABSZ: sym_sz=d.d_val; break;
            case MM_DT_SYMENT: case MM_DT_SCE_SYMENT: syment=d.d_val; break;
            case MM_DT_RELA: case MM_DT_SCE_RELA: if(!rela_va)rela_va=d.d_val; break;
            case MM_DT_RELASZ: case MM_DT_SCE_RELASZ: if(!rela_sz)rela_sz=d.d_val; break;
            case MM_DT_JMPREL: case MM_DT_SCE_JMPREL: if(!jmp_va)jmp_va=d.d_val; break;
            case MM_DT_PLTRELSZ: case MM_DT_SCE_PLTRELSZ: if(!jmp_sz)jmp_sz=d.d_val; break;
            default: break;
        }
    }

    uint64_t str_off=0,sym_off=0;
    int str_ok=str_va && str_sz && !vaddr_to_fileoff(&c,str_va,&str_off) && range_ok(size,str_off,1);
    int sym_ok=sym_va && syment>=sizeof(mm_elf64_sym_t) && !vaddr_to_fileoff(&c,sym_va,&sym_off) && range_ok(size,sym_off,sizeof(mm_elf64_sym_t));
    if (!sym_sz && sym_va && str_va>sym_va) sym_sz=str_va-sym_va;
    uint64_t symcount=(sym_ok && sym_sz>=syment)?sym_sz/syment:0;
    if (symcount>MM_MAX_SYMBOLS) symcount=MM_MAX_SYMBOLS;

    for (uint64_t i=0;i<dcount;i++) {
        uint64_t off=dyn_off+i*sizeof(mm_elf64_dyn_t); if (!range_ok(size,off,sizeof(mm_elf64_dyn_t))) break;
        mm_elf64_dyn_t d; memcpy(&d,data+off,sizeof(d));
        if (d.d_tag==MM_DT_NULL) break;
        fputs("{\"record\":\"dynamic\",\"path\":",f); mm_json_cstr(f,path);
        fprintf(f,",\"index\":%llu,\"tag\":\"0x%016llx\",\"tag_name\":",(unsigned long long)i,(unsigned long long)d.d_tag);
        mm_json_cstr(f,dyn_tag_name(d.d_tag)); fprintf(f,",\"value\":\"0x%016llx\"}\n",(unsigned long long)d.d_val);
        stats->dynamic_entries++;

        if (!str_ok) continue;
        const unsigned char *sp; size_t sl;
        if (d.d_tag==MM_DT_NEEDED || d.d_tag==MM_DT_SONAME || d.d_tag==MM_DT_SCE_ORIGFILENAME) {
            if (!string_at(&c,str_off,str_sz,d.d_val,&sp,&sl)) {
                const char *kind=d.d_tag==MM_DT_NEEDED?"needed_file":(d.d_tag==MM_DT_SONAME?"soname":"orig_filename");
                fputs("{\"record\":\"string_ref\",\"path\":",f); mm_json_cstr(f,path); fputs(",\"kind\":",f); mm_json_cstr(f,kind); fputs(",\"name\":",f); mm_json_string(f,sp,sl); fputs("}\n",f);
                if (d.d_tag==MM_DT_NEEDED) stats->dependencies++;
            }
        } else if (d.d_tag==MM_DT_SCE_NEEDED_MODULE || d.d_tag==MM_DT_SCE_PS5_IMPORT_MODULE ||
                   d.d_tag==MM_DT_SCE_IMPLIB || d.d_tag==MM_DT_SCE_PS5_IMPORT_LIB ||
                   d.d_tag==MM_DT_SCE_EXPLIB || d.d_tag==MM_DT_SCE_PS5_EXPORT_LIB ||
                   d.d_tag==MM_DT_SCE_MODULEINFO || d.d_tag==MM_DT_SCE_PS5_MODULEINFO) {
            uint64_t noff=(uint32_t)d.d_val;
            if (!string_at(&c,str_off,str_sz,noff,&sp,&sl)) {
                const char *kind="object_ref";
                if (d.d_tag==MM_DT_SCE_NEEDED_MODULE || d.d_tag==MM_DT_SCE_PS5_IMPORT_MODULE) kind="import_module";
                else if (d.d_tag==MM_DT_SCE_IMPLIB || d.d_tag==MM_DT_SCE_PS5_IMPORT_LIB) kind="import_library";
                else if (d.d_tag==MM_DT_SCE_EXPLIB || d.d_tag==MM_DT_SCE_PS5_EXPORT_LIB) kind="export_library";
                else if (d.d_tag==MM_DT_SCE_MODULEINFO || d.d_tag==MM_DT_SCE_PS5_MODULEINFO) kind="module_info";
                emit_named_dyn(out,path,kind,d.d_val,sp,sl);
                if (d.d_tag==MM_DT_SCE_NEEDED_MODULE || d.d_tag==MM_DT_SCE_PS5_IMPORT_MODULE) stats->dependencies++;
            }
        }
    }

    if (str_ok && sym_ok && symcount) {
        for (uint64_t i=1;i<symcount;i++) {
            uint64_t off=sym_off+i*syment; if (!range_ok(size,off,sizeof(mm_elf64_sym_t))) break;
            mm_elf64_sym_t s; memcpy(&s,data+off,sizeof(s));
            if (s.st_name>=str_sz) continue;
            const unsigned char *sp; size_t sl; if (string_at(&c,str_off,str_sz,s.st_name,&sp,&sl)) continue;
            if (!sl) continue;
            int is_import=(s.st_shndx==MM_SHN_UNDEF);
            int is_export=!is_import && (mm_elf_st_bind(s.st_info)==1 || mm_elf_st_bind(s.st_info)==2);
            fputs("{\"record\":\"symbol\",\"path\":",f); mm_json_cstr(f,path);
            fprintf(f,",\"index\":%llu,\"name\":",(unsigned long long)i); mm_json_string(f,sp,sl);
            fprintf(f,",\"bind\":%u,\"type\":%u,\"shndx\":%u,\"value\":\"0x%016llx\",\"size\":%llu,\"classification\":\"%s\"}\n",
                    (unsigned)mm_elf_st_bind(s.st_info),(unsigned)mm_elf_st_type(s.st_info),(unsigned)s.st_shndx,
                    (unsigned long long)s.st_value,(unsigned long long)s.st_size,is_import?"import":(is_export?"export":"defined"));
            stats->symbols++; if(is_import)stats->imports++; else if(is_export)stats->exports++;
        }
    }

    if (sym_ok && str_ok) {
        emit_reloc_table(&c,path,out,stats,rela_va,rela_sz,"rela",sym_off,syment,symcount,str_off,str_sz);
        emit_reloc_table(&c,path,out,stats,jmp_va,jmp_sz,"jmprel",sym_off,syment,symcount,str_off,str_sz);
    }
    stats->images_parsed++;
    return 0;
}

#pragma once
#include <stdint.h>
#include <stddef.h>

#define MM_ELF_MAGIC_0 0x7f
#define MM_ELF_MAGIC_1 'E'
#define MM_ELF_MAGIC_2 'L'
#define MM_ELF_MAGIC_3 'F'
#define MM_EM_X86_64 0x3e

#define MM_PT_LOAD            1u
#define MM_PT_DYNAMIC         2u
#define MM_PT_INTERP          3u
#define MM_PT_NOTE            4u
#define MM_PT_SHLIB           5u
#define MM_PT_PHDR            6u
#define MM_PT_TLS             7u
#define MM_PT_SCE_DYNLIBDATA  0x61000000u
#define MM_PT_SCE_PROCPARAM   0x61000001u
#define MM_PT_SCE_MODULEPARAM 0x61000002u
#define MM_PT_SCE_RELRO       0x61000010u
#define MM_PT_GNU_EH_FRAME    0x6474e550u
#define MM_PT_GNU_STACK       0x6474e551u
#define MM_PT_GNU_RELRO       0x6474e552u
#define MM_PT_SCE_COMMENT     0x6fffff00u
#define MM_PT_SCE_LIBVERSION  0x6fffff01u

#define MM_DT_NULL                 0
#define MM_DT_NEEDED               1
#define MM_DT_PLTRELSZ             2
#define MM_DT_PLTGOT               3
#define MM_DT_HASH                 4
#define MM_DT_STRTAB               5
#define MM_DT_SYMTAB               6
#define MM_DT_RELA                 7
#define MM_DT_RELASZ               8
#define MM_DT_RELAENT              9
#define MM_DT_STRSZ               10
#define MM_DT_SYMENT              11
#define MM_DT_INIT                12
#define MM_DT_FINI                13
#define MM_DT_SONAME              14
#define MM_DT_RPATH               15
#define MM_DT_SYMBOLIC            16
#define MM_DT_REL                 17
#define MM_DT_RELSZ               18
#define MM_DT_RELENT              19
#define MM_DT_PLTREL              20
#define MM_DT_DEBUG               21
#define MM_DT_TEXTREL             22
#define MM_DT_JMPREL              23
#define MM_DT_BIND_NOW            24
#define MM_DT_INIT_ARRAY          25
#define MM_DT_FINI_ARRAY          26
#define MM_DT_INIT_ARRAYSZ        27
#define MM_DT_FINI_ARRAYSZ        28
#define MM_DT_RUNPATH             29
#define MM_DT_FLAGS               30
#define MM_DT_PREINIT_ARRAY       32
#define MM_DT_PREINIT_ARRAYSZ     33

#define MM_DT_SCE_FINGERPRINT      0x61000007LL
#define MM_DT_SCE_ORIGFILENAME     0x61000009LL
#define MM_DT_SCE_MODULEINFO       0x6100000dLL
#define MM_DT_SCE_NEEDED_MODULE    0x6100000fLL
#define MM_DT_SCE_MODULE_ATTR      0x61000011LL
#define MM_DT_SCE_EXPLIB           0x61000013LL
#define MM_DT_SCE_IMPLIB           0x61000015LL
#define MM_DT_SCE_EXPORT_LIB_ATTR  0x61000017LL
#define MM_DT_SCE_IMPORT_LIB_ATTR  0x61000019LL
#define MM_DT_SCE_HASH             0x61000025LL
#define MM_DT_SCE_PLTGOT           0x61000027LL
#define MM_DT_SCE_JMPREL           0x61000029LL
#define MM_DT_SCE_PLTREL           0x6100002bLL
#define MM_DT_SCE_PLTRELSZ         0x6100002dLL
#define MM_DT_SCE_RELA             0x6100002fLL
#define MM_DT_SCE_RELASZ           0x61000031LL
#define MM_DT_SCE_RELAENT          0x61000033LL
#define MM_DT_SCE_STRTAB           0x61000035LL
#define MM_DT_SCE_STRSIZE          0x61000037LL
#define MM_DT_SCE_SYMTAB           0x61000039LL
#define MM_DT_SCE_SYMENT           0x6100003bLL
#define MM_DT_SCE_HASHSZ           0x6100003dLL
#define MM_DT_SCE_SYMTABSZ         0x6100003fLL
#define MM_DT_SCE_PS5_ORIGFILENAME  0x61000041LL
#define MM_DT_SCE_PS5_MODULEINFO    0x61000043LL
#define MM_DT_SCE_PS5_IMPORT_MODULE 0x61000045LL
#define MM_DT_SCE_PS5_EXPORT_LIB    0x61000047LL
#define MM_DT_SCE_PS5_IMPORT_LIB    0x61000049LL
#define MM_DT_RELACOUNT             0x6ffffff9LL

#define MM_SELF_MAGIC_PS5 0x5414f5eeu
#define MM_SELF_MAGIC_PS4 0x4f153d1du
#define MM_SELF_SEGMENT_FLAG_ENCRYPTED  0x2ULL
#define MM_SELF_SEGMENT_FLAG_COMPRESSED 0x8ULL
#define MM_SELF_SEGMENT_FLAG_DATA       0x800ULL

#define MM_SHN_UNDEF 0u

#pragma pack(push, 1)
typedef struct mm_elf64_ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} mm_elf64_ehdr_t;

typedef struct mm_elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} mm_elf64_phdr_t;

typedef struct mm_elf64_dyn {
    int64_t d_tag;
    uint64_t d_val;
} mm_elf64_dyn_t;

typedef struct mm_elf64_sym {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} mm_elf64_sym_t;

typedef struct mm_elf64_rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} mm_elf64_rela_t;

typedef struct mm_self_header {
    uint32_t magic_be;
    uint8_t version;
    uint8_t mode;
    uint8_t endian;
    uint8_t attributes;
    uint32_t key_type;
    uint16_t header_size;
    uint16_t meta_size;
    uint64_t file_size;
    uint16_t num_segments;
    uint16_t flags;
    uint32_t reserved;
} mm_self_header_t;

typedef struct mm_self_segment {
    uint64_t flags;
    uint64_t file_offset;
    uint64_t file_size;
    uint64_t mem_size;
} mm_self_segment_t;
#pragma pack(pop)

static inline uint32_t mm_be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline uint32_t mm_elf64_r_sym(uint64_t info) { return (uint32_t)(info >> 32); }
static inline uint32_t mm_elf64_r_type(uint64_t info) { return (uint32_t)info; }
static inline uint8_t mm_elf_st_bind(uint8_t info) { return (uint8_t)(info >> 4); }
static inline uint8_t mm_elf_st_type(uint8_t info) { return (uint8_t)(info & 0x0f); }

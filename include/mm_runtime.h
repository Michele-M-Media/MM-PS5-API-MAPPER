#pragma once

#include "mm_mapper.h"
#include <stdint.h>
#include <stddef.h>

/*
 * Read-only runtime dynlib + process/thread/VM/resource census.
 *
 * These structures mirror the current ps5-payload-dev/sdk CRT dynlib metadata
 * layouts (crt/kernel.h).  They are only dereferenced through kernel_copyout;
 * this mapper never writes kernel/process memory and never loads modules.
 */

typedef struct mm_dynlib_dynsec {
    struct {
        uint64_t le_next;
        uint64_t le_prev;
    } list_entry;

    uint64_t sysvec;
    uint32_t refcount;
    uint32_t _pad_refcount;
    uint64_t size;

    uint64_t symtab;
    uint64_t symtabsize;
    uint64_t strtab;
    uint64_t strtabsize;
    uint64_t pltrela;
    uint64_t pltrelasize;
    uint64_t rela;
    uint64_t relasize;
    uint64_t hash;
    uint64_t hashsize;
    uint64_t dynamic;
    uint64_t dynamicsize;
    uint64_t sce_comment;
    uint64_t sce_commentsize;
    uint64_t sce_dynlib;
    uint64_t sce_dynlibsize;
    uint64_t unknown1;
    uint64_t unknown1size;
    uint64_t buckets;
    uint64_t bucketssize;
    uint32_t nbuckets;
    uint32_t _pad_nbuckets;
    uint64_t chains;
    uint64_t chainssize;
    uint32_t nchains;
    uint32_t _pad_nchains;
    uint64_t unknown2[7];
} mm_dynlib_dynsec_t;

typedef struct mm_dynlib_obj {
    uint64_t next;
    uint64_t path;
    uint64_t unknown0[2];
    uint32_t refcount;
    uint32_t _pad_refcount;
    uint64_t handle;

    uint64_t mapbase;
    uint64_t mapsize;
    uint64_t textsize;
    uint64_t database;
    uint64_t datasize;
    uint64_t unknown1;
    uint64_t unknown1size;
    uint64_t entry;
    uint64_t unknown2;
    uint64_t vaddrbase;

    uint32_t tlsindex;
    uint32_t _pad_tlsindex;
    uint64_t tlsinit;
    uint64_t tlsinitsize;
    uint64_t tlssize;
    uint64_t tlsoffset;
    uint64_t tlsalign;
    uint64_t pltgot;

    uint64_t unknown3[6];
    uint64_t init;
    uint64_t fini;
    uint64_t eh_frame_hdr;
    uint64_t eh_frame_hdr_size;
    uint64_t eh_frame;
    uint64_t eh_frame_size;

    int32_t status;
    int32_t flags;

    uint64_t unknown4[5];
    uint64_t dynsec;
    uint64_t unknown5[6];
} mm_dynlib_obj_t;

int mm_runtime_scan_all_processes(mm_jsonl_t *out, mm_stats_t *stats);

#pragma once
#include <stdint.h>
#include <stdio.h>

typedef struct mm_stats {
    uint64_t roots_seen;
    uint64_t roots_opened;
    uint64_t dirs_seen;
    uint64_t files_seen;
    uint64_t executable_candidates;
    uint64_t images_parsed;
    uint64_t raw_elf;
    uint64_t self_images;
    uint64_t self_encrypted_segments;
    uint64_t self_compressed_segments;
    uint64_t parse_errors;
    uint64_t dynamic_entries;
    uint64_t symbols;
    uint64_t imports;
    uint64_t exports;
    uint64_t relocations;
    uint64_t dependencies;
    uint64_t devices;
    uint64_t root_entries;
    uint64_t device_entries;
    uint64_t filesystem_dirs_emitted;
    uint64_t extension_candidates;
    uint64_t large_image_candidates;
    uint64_t runtime_processes;
    uint64_t runtime_modules;
    uint64_t runtime_dynsecs;
    uint64_t runtime_dynamic_entries;
    uint64_t runtime_symbols;
    uint64_t runtime_imports;
    uint64_t runtime_exports;
    uint64_t runtime_relocations;
    uint64_t runtime_dependencies;
    uint64_t runtime_errors;
    uint64_t runtime_bytes_copied;
    uint64_t runtime_strtab_strings;
    uint64_t runtime_raw_blobs;
    uint64_t runtime_raw_chunks;
    uint64_t runtime_raw_bytes;
    uint64_t runtime_library_attrs;
    uint64_t runtime_process_resources;
    uint64_t runtime_threads;
    uint64_t runtime_vmspaces;
    uint64_t runtime_vm_regions;
    uint64_t runtime_resource_anchors;
    uint64_t runtime_system_anchors;
    uint64_t runtime_fd_tables;
    uint64_t runtime_fds;
    uint64_t runtime_fd_data_raw;
    uint64_t runtime_fd_data_raw_bytes;
    uint64_t runtime_fd_data_pointer_candidates;
    uint64_t runtime_bus_devices;
    uint64_t runtime_bus_nodes_walked;
    uint64_t runtime_bus_raw_candidates;
    uint64_t runtime_bus_candidate_strings;
    uint64_t runtime_bus_list_terminators;
    uint64_t runtime_bus_snapshot_stable;
    uint64_t runtime_bus_drivers;
    uint64_t runtime_bus_driver_methods;
    uint64_t runtime_bus_softc_heads;
    uint64_t runtime_bus_softc_bytes;
    uint64_t runtime_observable_limits;
} mm_stats_t;

typedef struct mm_jsonl {
    FILE *fp;
} mm_jsonl_t;

void mm_json_string(FILE *fp, const unsigned char *s, size_t len);
void mm_json_cstr(FILE *fp, const char *s);
void mm_emit_error(mm_jsonl_t *out, const char *path, const char *stage, const char *message, int errnum);
int mm_parse_image(const char *path, const unsigned char *data, size_t size,
                   const char *sha256_hex, mm_jsonl_t *out, mm_stats_t *stats,
                   char *errbuf, size_t errbuf_len);

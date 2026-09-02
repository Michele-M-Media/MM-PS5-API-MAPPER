#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p output/host
cc -std=c11 -Wall -Wextra -Werror -O2 -Iinclude -o output/host/test_parser tests/test_parser.c src/elf_parser.c src/jsonl.c src/sha256.c
./output/host/test_parser
cc -std=c11 -Wall -Wextra -Werror -O2 -Iinclude -o output/host/mm_mapper_host_compile src/main.c src/elf_parser.c src/runtime_mapper.c src/jsonl.c src/sha256.c
python3 -m py_compile tools/generate_sdk_db.py tools/generate_header_db.py tools/fetch_aerolib.py tools/resolve_map.py tools/compare_maps.py tools/compare_runtime_graph.py

mkdir -p output/host/fake_sdk/ps5
cat > output/host/fake_sdk/ps5/kernel.h <<'FAKEPS5'
#pragma once
#include <stdint.h>
#include <stddef.h>
extern const intptr_t KERNEL_ADDRESS_TEXT_BASE;
extern const intptr_t KERNEL_ADDRESS_ALLPROC;
extern const intptr_t KERNEL_ADDRESS_ROOTVNODE;
extern const intptr_t KERNEL_ADDRESS_BUS_DATA_DEVICES;
extern const size_t KERNEL_OFFSET_PROC_P_PID;
extern const size_t KERNEL_OFFSET_PROC_P_FD;
extern const size_t KERNEL_OFFSET_PROC_P_VMSPACE;
extern const size_t KERNEL_OFFSET_FILEDESC_FD_RDIR;
extern const size_t KERNEL_OFFSET_FILEDESC_FD_JDIR;
extern unsigned long KERNEL_OFFSET_VMSPACE_P_ROOT;
uint32_t kernel_get_fw_version(void);
int32_t kernel_copyout(intptr_t kaddr, void *uaddr, size_t len);
FAKEPS5
cc -std=c11 -Wall -Wextra -Werror -O2 -Ioutput/host/fake_sdk -Iinclude -c src/runtime_mapper.c -o output/host/runtime_mapper_ps5path.o
cc -std=c11 -Wall -Wextra -Werror -O2 -Ioutput/host/fake_sdk -Iinclude -c src/main.c -o output/host/main_ps5path.o
echo "PS5_RUNTIME_BRANCH_COMPILE=PASS"
cc -std=c11 -Wall -Wextra -Werror -O2 -Ioutput/host/fake_sdk -Iinclude -o output/host/test_runtime tests/test_runtime.c src/runtime_mapper.c src/jsonl.c
./output/host/test_runtime
TMP="output/host/sdk_layout_test"
rm -rf "$TMP"
mkdir -p "$TMP/source/sce_stubs" "$TMP/installed/target/lib" "$TMP/installed/include/ps5"
printf '%s\n' 'asm(".global sceMapperSourceProbe\\n");' > "$TMP/source/sce_stubs/libSceMapperProbe.c"
python3 tools/generate_sdk_db.py --sdk "$TMP/source" --out "$TMP/source.csv" >/dev/null
grep -q 'sceMapperSourceProbe' "$TMP/source.csv"
printf '%s\n' 'void sceMapperInstalledProbe(void) {}' > "$TMP/probe.c"
printf '%s\n' 'int sceMapperInstalledProbe(int value);' > "$TMP/installed/include/ps5/mapper.h"
cc -shared -fPIC -o "$TMP/installed/target/lib/libSceMapperProbe.so" "$TMP/probe.c"
python3 tools/generate_sdk_db.py --sdk "$TMP/installed" --out "$TMP/installed.csv" >/dev/null
grep -q 'sceMapperInstalledProbe' "$TMP/installed.csv"
python3 tools/generate_header_db.py --sdk "$TMP/installed" --api-db "$TMP/installed.csv" --out "$TMP/prototypes.csv" >/dev/null
grep -q 'int sceMapperInstalledProbe(int value);' "$TMP/prototypes.csv"
NID=$(awk -F, 'NR==2{print $1}' "$TMP/installed.csv")
cat > "$TMP/synthetic_map.jsonl" <<SYNMAP
{"record":"header","tool":"MM-PS5-API-MAPPER","version":"host-test","firmware_raw":"0x00000000"}
{"record":"runtime_process","pid":123,"proc_address":"0xffff800000002000","dynlib_list_head":"0xffff800000003000"}
{"record":"runtime_process_resources","pid":123,"proc_address":"0xffff800000002000","thread_head":"0xffff80000000a000","filedesc":"0xffff80000000b000","fd_files":"0xffff80000000c000","root_vnode":"0xffff800000012000","jail_vnode":"0xffff800000013000","vmspace":"0xffff80000000d000","dynlib_list_head":"0xffff800000003000","thread_read_rc":0,"filedesc_read_rc":0,"vmspace_read_rc":0,"dynlib_read_rc":0,"policy":"read-only anchors; credentials excluded"}
{"record":"runtime_thread","pid":123,"thread_address":"0xffff80000000a000","tid":456,"tid_raw":"0x00000000000001c8","next":"0x0000000000000000","layout_source":"ps5-payload-dev/sdk kernel_get_proc_thread"}
{"record":"runtime_vmspace","pid":123,"vmspace":"0xffff80000000d000","root_offset":464,"vm_root":"0xffff80000000e000","layout_source":"ps5-payload-dev/sdk kernel_get_vmem_entry"}
{"record":"runtime_vm_region","pid":123,"entry_address":"0xffff80000000e000","start":"0x0000000800000000","end":"0x0000000800020000","size":131072,"prot_raw":5,"next":"0x0000000000000000"}
{"record":"runtime_system_anchor","kind":"bus_data_devices","address":"0xffff800000011000","first_raw":"0xffff800000014000","read_rc":0,"layout":"ANCHOR-ONLY","raw16":"001400000080ffff0000000000000000"}
{"record":"runtime_fd_table","pid":123,"filedesc":"0xffff80000000b000","fd_files":"0xffff80000000c000","nfiles_raw":3,"read_rc":0,"entries_offset":8,"entry_stride":48,"layout_source":"ps5-payload-dev/sdk kernel_get_proc_file + FreeBSD fdescenttbl"}
{"record":"runtime_fd","pid":123,"fd":1,"entry_address":"0xffff80000000c038","fde_file":"0xffff800000015000","file_data":"0xffff800000016000","file_data_read_rc":0,"layout_source":"ps5-payload-dev/sdk kernel_get_proc_file","policy":"no struct-file credential decode"}
{"record":"runtime_fd_data_raw","pid":123,"fd":1,"file_data":"0xffff800000016000","size":256,"read_rc":0,"interpretation":"RAW-RESOURCE-OBJECT-NO-SEMANTIC-DECODE","hex":"00"}
{"record":"runtime_fd_data_pointer_candidate","pid":123,"fd":1,"file_data":"0xffff800000016000","offset":0,"pointer":"0xffff800000014000","evidence":"raw aligned qword only; pointer not dereferenced"}
{"record":"runtime_fd_data_pointer_candidate","pid":123,"fd":1,"file_data":"0xffff800000016000","offset":8,"pointer":"0xffff800000019000","evidence":"raw aligned qword only; pointer not dereferenced"}
{"record":"runtime_fd","pid":123,"fd":2,"entry_address":"0xffff80000000c068","fde_file":"0xffff800000015100","file_data":"0xffff800000017000","file_data_read_rc":0,"layout_source":"ps5-payload-dev/sdk kernel_get_proc_file","policy":"no struct-file credential decode"}
{"record":"runtime_fd_data_raw","pid":123,"fd":2,"file_data":"0xffff800000017000","size":256,"read_rc":0,"interpretation":"RAW-RESOURCE-OBJECT-NO-SEMANTIC-DECODE","hex":"00"}
{"record":"runtime_fd_data_pointer_candidate","pid":123,"fd":1,"file_data":"0xffff800000016000","offset":16,"pointer":"0xffff800000017000","evidence":"raw aligned qword only; pointer not dereferenced"}
{"record":"runtime_bus_list_head","address":"0xffff800000011000","first":"0xffff800000014000","tail_next_slot":"0xffff800000014018","expected_last_device":"0xffff800000014000","layout_source":"FreeBSD TAILQ_HEAD lineage + PS5-source-grounded global-next +0x18"}
{"record":"runtime_bus_device_raw_candidate","index":1,"device_address":"0xffff80000001e000","next_ps5_confirmed":"0x0","nameunit_pointer_at_0x58":"0x0","nameunit_read_rc":-1,"expected_last_device":"0xffff80000001e000","reason":"host synthetic raw continuation","raw_prefix":"00000000000000000000000000000000000000000000000000000000000000000000000000000000004001000080ffff00000000000000000000000000000000009001000080ffff0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000d001000080ffff"}
{"record":"runtime_bus_candidate_string","device_address":"0xffff80000001e000","offset":64,"pointer":"0xffff80000001a000","value":"testbus","evidence":"bounded qword pointer -> readable ASCII; semantic field not assigned"}
{"record":"runtime_bus_list_terminator","device_address":"0xffff800000014000","expected_last_device":"0xffff800000014000","kind":"null_next_decoded","matches_tailq_expected_last":true}
{"record":"runtime_bus_list_consistency","head_read_rc":0,"first_before":"0xffff800000014000","first_after":"0xffff800000014000","tail_slot_before":"0xffff800000014018","tail_slot_after":"0xffff800000014018","stable":true,"ended_with_null":true}
{"record":"runtime_bus_device","index":0,"device_address":"0xffff800000014000","next":"0x0","parent":"0x0","driver":"0xffff800000019000","devclass":"0x0","unit_raw":0,"nameunit":"testbus0","desc":"Synthetic test bus","busy_raw":0,"state_raw":2,"devflags_raw":0,"flags_raw":0,"order_raw":0,"ivars":"0x0","softc":"0xffff80000001d000","layout_confidence":"PS5-SOURCE-GROUNDED-NAMEUNIT-SOFTC-NEXT; FREEBSD-LINEAGE-PREFIX","raw_prefix":"00"}
{"record":"runtime_bus_driver","driver_address":"0xffff800000019000","name":"testbus","methods":"0xffff80000001b000","class_size_raw":256,"baseclasses":"0x0","refs_raw":1,"ops":"0x0","layout_source":"FreeBSD kobj_class; runtime string validated"}
{"record":"runtime_bus_driver_method","driver_address":"0xffff800000019000","driver_name":"testbus","index":0,"method_address":"0xffff80000001b000","desc":"0xffff80000001c000","desc_id_raw":77,"desc_id_read_rc":0,"func":"0xffff800000001234","default_func_raw":"0xffff800000002222","kernel_text_offset":"0x1234","layout_source":"FreeBSD kobj_method/kobjop_desc; bounded read-only"}
{"record":"runtime_bus_softc_head","device_address":"0xffff800000014000","nameunit":"testbus0","softc":"0xffff80000001d000","size":256,"interpretation":"RAW-UNKNOWN-READ-ONLY","hex":"00"}
{"record":"runtime_module","pid":123,"path":"/system/common/lib/libSceMapperProbe.sprx","handle":"0x42","mapbase":"0x800000000","mapsize":65536,"textsize_raw":32768,"database_raw":"0x0","datasize_raw":8192,"entry_raw":"0x800001000","vaddrbase_raw":"0x800000000","tlsindex_raw":0,"pltgot_raw":"0x0","init_raw":"0x0","fini_raw":"0x0","status_raw":1,"flags_raw":0,"dynsec":"0xffff800000006000","prefix_layout_confidence":"HIGH","extended_layout_sane":true,"extended_layout_confidence":"PLAUSIBLE"}
{"record":"runtime_library_ref","pid":123,"path":"/system/common/lib/libSceMapperProbe.sprx","kind":"import_library","object_id":23,"version_major":1,"version_minor":0,"name":"libSceMapperProbe","raw":"0x0"}
{"record":"runtime_library_ref","pid":123,"path":"/system/common/lib/libSceMapperProbe.sprx","kind":"import_module","object_id":24,"version_major":1,"version_minor":1,"name":"libSceMapperProbe","raw":"0x0"}
{"record":"runtime_symbol","pid":123,"path":"/system/common/lib/libSceMapperProbe.sprx","index":1,"name":"${NID}#X#Y","nid":"${NID}","classification":"import","value":"0x0","size":0}
{"record":"summary","runtime_processes":1,"runtime_modules":1,"runtime_symbols":1}
SYNMAP
python3 tools/resolve_map.py --map "$TMP/synthetic_map.jsonl" --sdk-db "$TMP/installed.csv" --prototype-db "$TMP/prototypes.csv" --outdir "$TMP/resolved" >/dev/null
grep -q 'sceMapperInstalledProbe' "$TMP/resolved/full_api_map.csv"
grep -q 'runtime' "$TMP/resolved/full_api_map.csv"
grep -q 'provider-exact' "$TMP/resolved/full_api_map.csv"
grep -q 'libSceMapperProbe' "$TMP/resolved/full_api_map.csv"
grep -q 'int sceMapperInstalledProbe(int value);' "$TMP/prototypes.csv"
grep -q '456' "$TMP/resolved/runtime_threads.csv"
grep -q '0x0000000800000000' "$TMP/resolved/runtime_vm_regions.csv"
grep -q '100.00' "$TMP/resolved/runtime_module_vm_links.csv"
grep -q 'libSceMapperProbe' "$TMP/resolved/api_provider_consumer_edges.csv"
grep -q 'credentials excluded' "$TMP/resolved/runtime_process_resources.csv"
grep -q 'testbus0' "$TMP/resolved/runtime_bus_devices.csv"
grep -q 'host synthetic raw continuation' "$TMP/resolved/runtime_bus_raw_candidates.csv"
grep -q 'testbus' "$TMP/resolved/runtime_bus_candidate_strings.csv"
grep -q 'expected_last_device' "$TMP/resolved/runtime_bus_list_heads.csv"
grep -q 'null_next_decoded' "$TMP/resolved/runtime_bus_list_terminators.csv"
grep -q 'True\|true' "$TMP/resolved/runtime_bus_list_consistency.csv"
grep -q 'testbus' "$TMP/resolved/runtime_bus_drivers.csv"
grep -q '77' "$TMP/resolved/runtime_bus_driver_methods.csv"
grep -q ',1,' "$TMP/resolved/runtime_fds.csv"
grep -q 'RAW-RESOURCE-OBJECT-NO-SEMANTIC-DECODE' "$TMP/resolved/runtime_fd_data_raw.csv"
grep -q 'RESOURCE-TO-RESOURCE' "$TMP/resolved/fd_resource_to_resource_edges.csv"
test -s "$TMP/resolved/fd_resource_signature_groups.csv"
test -s "$TMP/resolved/fd_resource_components.csv"
grep -q 'bus_driver' "$TMP/resolved/fd_resource_pointer_matches.csv"
grep -q 'FIELD-SEMANTICS-UNASSIGNED' "$TMP/resolved/bus_raw_pointer_matches.csv"
grep -q 'EXACT-KERNEL-POINTER' "$TMP/resolved/hardware_resource_graph.csv"
grep -q 'testbus0' "$TMP/resolved/bus_device_driver_edges.csv"
grep -q 'testbus0' "$TMP/resolved/runtime_bus_graph.csv"
grep -q 'CONTROLLER/HID\|OTHER/UNKNOWN' "$TMP/resolved/hardware_bus_surface.csv"
echo "FULL_RESOLVER_PIPELINE=PASS"
mkdir -p "$TMP/empty"
python3 tools/generate_sdk_db.py --sdk "$TMP/empty" --out "$TMP/empty.csv" >/dev/null
test -s "$TMP/empty.csv"
echo "SDK_LAYOUT_FALLBACK=PASS"
echo "RUNTIME_LAYOUT_STATIC_ASSERTS=PASS"
echo "HOST_VALIDATION=PASS"

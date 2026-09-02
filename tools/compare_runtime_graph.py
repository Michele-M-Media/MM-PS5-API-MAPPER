#!/usr/bin/env python3
import argparse,csv
from pathlib import Path

def read_rows(path):
    p=Path(path)
    if not p.exists(): return []
    with p.open(encoding='utf-8',errors='replace',newline='') as f:
        return list(csv.DictReader(f))

def keys(rows, fields):
    return {tuple(r.get(k,'') for k in fields) for r in rows}

def section(f,title,a,b):
    add=sorted(b-a); rem=sorted(a-b)
    f.write(f'## {title}\n\n- Added: **{len(add)}**\n- Removed: **{len(rem)}**\n\n')
    if add:
        f.write('### Added\n\n')
        for x in add[:500]: f.write(f'- `{x}`\n')
        if len(add)>500:f.write(f'- ... {len(add)-500} more\n')
    if rem:
        f.write('\n### Removed\n\n')
        for x in rem[:500]: f.write(f'- `{x}`\n')
        if len(rem)>500:f.write(f'- ... {len(rem)-500} more\n')
    f.write('\n')

def main():
    ap=argparse.ArgumentParser(description='Compare two resolved resource/device graph directories')
    ap.add_argument('base_dir');ap.add_argument('new_dir');ap.add_argument('--out',default='RUNTIME_GRAPH_DIFF.md');a=ap.parse_args()
    A=Path(a.base_dir);B=Path(a.new_dir)
    specs=[
      ('Processes','runtime_process_graph.csv',['pid','root_module']),
      ('Threads','runtime_threads.csv',['pid','tid']),
      ('VM regions','runtime_vm_regions.csv',['pid','start','end','prot_raw']),
      ('Modules','runtime_modules.csv',['pid','canonical_path','mapbase','mapsize']),
      ('API provider/consumer edges','api_provider_consumer_edges.csv',['source','pid','consumer_module','provider_library','provider_module','nid','api_name_or_raw']),
      ('Devices','device_nodes.csv',['path','kind','rdev']),
      ('Open FDs','runtime_fds.csv',['pid','fd','fde_file','file_data']),
      ('FD resource raw objects','runtime_fd_data_raw.csv',['pid','fd','file_data','read_rc']),
      ('FD resource pointer matches','fd_resource_pointer_matches.csv',['pid','fd','raw_offset','pointer','matched_entity','matched_name']),
      ('FD resource pointer clusters','fd_resource_pointer_clusters.csv',['raw_offset','pointer','occurrences','pid_count']),
      ('Kernel bus devices','runtime_bus_devices.csv',['device_address','nameunit','driver','softc','state_raw']),
      ('Kernel bus raw candidates','runtime_bus_raw_candidates.csv',['device_address','next_ps5_confirmed','nameunit_pointer_at_0x58']),
      ('Kernel bus candidate strings','runtime_bus_candidate_strings.csv',['device_address','offset','pointer','value']),
      ('Kernel bus drivers','runtime_bus_drivers.csv',['driver_address','name','methods']),
      ('Kernel driver methods','runtime_bus_driver_methods.csv',['driver_name','desc_id_raw','func','kernel_text_offset']),
      ('Device-node/bus candidates','device_node_bus_candidates.csv',['device_node','bus_nameunit','driver_name','reason']),
      ('Raw bus pointer matches','bus_raw_pointer_matches.csv',['raw_bus_device','raw_offset','pointer','matched_entity','matched_name']),
      ('Unified resource graph','hardware_resource_graph.csv',['source_type','source_id','target_type','target_id','evidence']),
    ]
    out=Path(a.out)
    with out.open('w',encoding='utf-8') as f:
        f.write('# MM PS5 v0.8 resource-chain-correlation hardware/runtime graph diff\n\n')
        f.write(f'- Base: `{A}`\n- New: `{B}`\n\n')
        for title,fn,fields in specs:
            section(f,title,keys(read_rows(A/fn),fields),keys(read_rows(B/fn),fields))
    print(f'[OK] {out}')
if __name__=='__main__':main()

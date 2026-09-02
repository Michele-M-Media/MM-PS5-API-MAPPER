#!/usr/bin/env python3
import argparse,csv,hashlib,json,re,struct
from collections import Counter,defaultdict
from pathlib import Path

CHARSET='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-'
CHARINDEX={c:i for i,c in enumerate(CHARSET)}
SUFFIX=bytes.fromhex('518D64A635DED8C1E6B039B1C3E55230')
NID_RE=re.compile(r'^([A-Za-z0-9+\-]{11})(?:#|$)')
RAND_SYS_RE=re.compile(r'^/[^/]+/(common|priv)/lib/(.+)$')
RAND_EX_RE=re.compile(r'^/[^/]+/(common_ex|priv_ex)/lib/(.+)$')

def encode_nid(name:str)->str:
    d=hashlib.sha1(name.encode('ascii')+SUFFIX).digest()[:8]
    enc=struct.unpack('<Q',d)[0]
    out=CHARSET[(enc&0xF)<<2]; x=enc>>4
    while x: out+=CHARSET[x&0x3F]; x>>=6
    return out[::-1]

def decode_obj_id(token:str):
    if not token or len(token)>4:return None
    v=0
    try:
        for c in token:v=(v<<6)|CHARINDEX[c]
    except KeyError:return None
    return v

def parse_symbol_suffix(raw:str):
    parts=(raw or '').split('#')
    if len(parts)<3:return None,None
    return decode_obj_id(parts[1]),decode_obj_id(parts[2])

def extract_nid(raw):
    if not raw:return ''
    m=NID_RE.match(raw); return m.group(1) if m else ''

def canonical_path(path:str)->str:
    if not path:return ''
    m=RAND_SYS_RE.match(path)
    if m:return f'/system/{m.group(1)}/lib/{m.group(2)}'
    m=RAND_EX_RE.match(path)
    if m:return f'/system_ex/{m.group(1)}/lib/{m.group(2)}'
    return path

def module_stem(path):
    b=Path(path).name
    for suf in ('.sprx','.prx','.so','.elf','.bin','.self'):
        if b.endswith(suf):return b[:-len(suf)]
    return b

def intish(v,default=0):
    if isinstance(v,int):return v
    if v is None:return default
    try:return int(str(v),0)
    except (ValueError,TypeError):return default

def possible_sdk_libs(path):
    stem=module_stem(canonical_path(path)); out={stem}
    if stem=='libkernel':out.update({'libkernel','libkernel_web','libkernel_sys'})
    if stem in ('libkernel_web','libkernel_sys'):out.add('libkernel')
    return out

def category(name,lib,path):
    s=' '.join([name or '',lib or '',path or '']).lower()
    groups=[
      ('CONTROLLER/HID',['dualsense','pad','hid','controller','kbemulate','mouse','keyboard','gamepad']),
      ('AUDIO/HAPTICS',['audio','ngs','ajm','haptic','vibration','speaker','microphone','mic','dsee','mbus']),
      ('VIDEO/DISPLAY/GPU',['videoout','avsetting','hdmi','display','hdr','gpu','agc','gnm','razor','sulpha']),
      ('STORAGE/FILESYSTEM',['storage','savedata','save','fsinternal','filesystem','mount','pfs','usb','nvme','appinst','bgft','bgs','disc']),
      ('NETWORK/REMOTEPLAY',['net','http','ssl','curl','websocket','wifi','ethernet','remoteplay','ipmi']),
      ('POWER/THERMAL',['power','thermal','fan','temperature','rtc','battery']),
      ('CAMERA/VISION',['camera','vision','depth']),
      ('USER/ACCOUNT/NP',['user','login','npmanager','account','npcommon','npsns','npweb']),
      ('APP/SYSTEM/SHELL',['app','systemservice','syscore','rnps','shell','regmgr','contentlist','resourcearbitrator','sysmodule']),
      ('SECURITY/TEE',['tee','secure','auth','crypto','dipsw','marlin','playready']),
      ('MEDIA/CODEC',['codec','vdec','venc','opus','jpeg','png','gif','media','dts','ac3','webm','mp4']),
      ('WEB/UI',['webkit','web','jsc','cairo','icu']),
    ]
    for cat,keys in groups:
        if any(k in s for k in keys):return cat
    return 'OTHER/UNKNOWN'

def load_sdk_rows(sdk:Path):
    try:
        from generate_sdk_db import build_rows
        rows,sources=build_rows(sdk)
    except Exception as e:
        print(f'[WARN] SDK name DB unavailable: {e}');return [],defaultdict(list),[]
    db=defaultdict(list)
    for nid,name,lib,origin in rows:db[nid].append((name,lib,origin))
    return rows,db,sources

def load_sdk_csv(path:Path):
    rows=[]; db=defaultdict(list)
    try:
        with path.open('r',encoding='utf-8',errors='replace',newline='') as f:
            for r in csv.DictReader(f):
                nid=r.get('nid','');name=r.get('name') or r.get('api_name','');lib=r.get('sdk_stub_library') or r.get('library','');origin=r.get('source','known-db')
                if not nid or not name:continue
                rows.append((nid,name,lib,origin));db[nid].append((name,lib,origin))
    except OSError as e:print(f'[WARN] SDK CSV unavailable: {e}')
    return rows,db,[str(path)] if rows else []

def load_prototypes(path:Path):
    d=defaultdict(list)
    if not path or not path.exists():return d
    try:
        with path.open('r',encoding='utf-8',errors='replace',newline='') as f:
            for r in csv.DictReader(f):
                n=r.get('api_name','');decl=r.get('declaration','');hdr=r.get('header','')
                if n and decl:d[n].append((decl,hdr))
    except OSError:pass
    return d

def choose_resolution(rows,provider):
    if not rows:return []
    if provider:
        exact=[r for r in rows if r[1]==provider]
        if exact:return [(r,'provider-exact') for r in exact]
        # libkernel aliases are commonly split in SDK databases.
        if provider in ('libkernel','libkernel_sys','libkernel_web'):
            ali=[r for r in rows if r[1] in ('libkernel','libkernel_sys','libkernel_web')]
            if ali:return [(r,'provider-alias') for r in ali]
    named=[r for r in rows if r[1]]
    if len(named)==1:return [(named[0],'nid-unique-library')]
    return [(r,'nid-known') for r in rows]

def write_csv(path,header,rows):
    with path.open('w',newline='',encoding='utf-8') as f:
        w=csv.writer(f);w.writerow(header);w.writerows(rows)

def main():
    ap=argparse.ArgumentParser(description='Resolve MM PS5 v0.8 resource-chain correlation graph with API/NID provider-consumer and local known-NID databases')
    ap.add_argument('--map',required=True)
    ap.add_argument('--sdk',default=None)
    ap.add_argument('--sdk-db',default=None)
    ap.add_argument('--prototype-db',default=None)
    ap.add_argument('--outdir',default=None)
    args=ap.parse_args()
    mp=Path(args.map); out=Path(args.outdir) if args.outdir else mp.with_suffix('').with_name(mp.stem+'_RESOLVED_V07');out.mkdir(parents=True,exist_ok=True)
    if args.sdk_db:sdk_rows,db,sdk_sources=load_sdk_csv(Path(args.sdk_db))
    elif args.sdk:sdk_rows,db,sdk_sources=load_sdk_rows(Path(args.sdk))
    else:
        sib=mp.parent/'sdk_api_db.csv';sdk_rows,db,sdk_sources=load_sdk_csv(sib) if sib.exists() else ([],defaultdict(list),[])
    proto_path=Path(args.prototype_db) if args.prototype_db else ((Path(args.sdk_db).parent/'sdk_api_prototypes.csv') if args.sdk_db else (mp.parent/'sdk_api_prototypes.csv'))
    prototypes=load_prototypes(proto_path)

    header={};summary={};counts=Counter();tag_counts=Counter()
    static_modules=[];runtime_modules=[];runtime_processes=[];process_resources=[];runtime_threads=[];runtime_vmspaces=[];runtime_vmregions=[];system_anchors=[];candidate_headers=[];raw_objects=[];raw_dynsecs=[];raw_tables=[];strtab_strings=[];lib_attrs=[];fs_dirs=[];ext_candidates=[];large_candidates=[]
    fd_tables=[];runtime_fds=[];fd_data_raw=[];fd_data_ptrs=[];bus_devices=[];bus_raw_candidates=[];bus_candidate_strings=[];bus_list_heads=[];bus_terminators=[];bus_consistency=[];bus_drivers=[];bus_methods=[];bus_softc=[];observable_limits=[]
    devices=[];gaps=[];library_rows=[];refs=defaultdict(lambda:defaultdict(dict));observed_raw=set();observed_canon=set();static_canon=set();runtime_canon=set()
    static_relocs=runtime_relocs=0
    # Pass 1: topology and provider-ID tables; do not retain million-line symbol/relocation payloads.
    with mp.open('r',encoding='utf-8',errors='replace') as f:
        for ln,line in enumerate(f,1):
            try:r=json.loads(line)
            except Exception as e:print(f'[WARN] JSON line {ln}: {e}');continue
            k=r.get('record','');counts[k]+=1
            if k=='header':header=r
            elif k=='summary':summary=r
            elif k=='image':
                static_modules.append(r);cp=canonical_path(r.get('path',''));static_canon.add(cp);observed_raw.add(r.get('path',''));observed_canon.add(cp)
            elif k=='runtime_module':
                runtime_modules.append(r);cp=canonical_path(r.get('path',''));runtime_canon.add(cp);observed_raw.add(r.get('path',''));observed_canon.add(cp)
            elif k=='runtime_process':runtime_processes.append(r)
            elif k=='runtime_process_resources':process_resources.append(r)
            elif k=='runtime_thread':runtime_threads.append(r)
            elif k=='runtime_vmspace':runtime_vmspaces.append(r)
            elif k=='runtime_vm_region':runtime_vmregions.append(r)
            elif k=='runtime_system_anchor':system_anchors.append(r)
            elif k=='runtime_fd_table':fd_tables.append(r)
            elif k=='runtime_fd':runtime_fds.append(r)
            elif k=='runtime_fd_data_raw':fd_data_raw.append(r)
            elif k=='runtime_fd_data_pointer_candidate':fd_data_ptrs.append(r)
            elif k=='runtime_bus_device':bus_devices.append(r)
            elif k=='runtime_bus_device_raw_candidate':bus_raw_candidates.append(r)
            elif k=='runtime_bus_candidate_string':bus_candidate_strings.append(r)
            elif k=='runtime_bus_list_head':bus_list_heads.append(r)
            elif k=='runtime_bus_list_terminator':bus_terminators.append(r)
            elif k=='runtime_bus_list_consistency':bus_consistency.append(r)
            elif k=='runtime_bus_driver':bus_drivers.append(r)
            elif k=='runtime_bus_driver_method':bus_methods.append(r)
            elif k=='runtime_bus_softc_head':bus_softc.append(r)
            elif k=='runtime_observable_limit':observable_limits.append(r)
            elif k=='candidate_header':candidate_headers.append(r)
            elif k=='runtime_object_raw':raw_objects.append(r)
            elif k=='runtime_dynsec_raw':raw_dynsecs.append(r)
            elif k=='runtime_raw_table_chunk':raw_tables.append(r)
            elif k=='runtime_strtab_string':strtab_strings.append(r)
            elif k=='runtime_library_attr':lib_attrs.append(r)
            elif k=='filesystem_dir':fs_dirs.append(r)
            elif k=='extension_candidate':ext_candidates.append(r)
            elif k=='large_image_candidate':large_candidates.append(r)
            elif k in ('library_ref','string_ref','runtime_library_ref','runtime_string_ref'):
                src='runtime' if k.startswith('runtime') else 'static';pid=r.get('pid','');path=r.get('path','');kind=r.get('kind','');name=r.get('name','');oid=r.get('object_id')
                library_rows.append([src,pid,path,canonical_path(path),kind,name,oid if oid is not None else '',r.get('version_major',''),r.get('version_minor',''),r.get('raw','')])
                if oid is not None and kind in ('import_library','export_library','import_module','module_info'):
                    refs[(pid,path)][kind][int(oid)]=name
            elif k=='device_node':devices.append(r)
            elif k in ('coverage_gap','runtime_coverage_gap','error'):gaps.append(r)
            elif k=='runtime_relocation':runtime_relocs+=1
            elif k=='relocation':static_relocs+=1
            elif k in ('runtime_dynamic','dynamic'):tag_counts[r.get('tag_name','UNKNOWN')]+=1

    # Process identity from handle-0/first module.
    proc_root={}
    for m in runtime_modules:
        pid=m.get('pid');cur=proc_root.get(pid)
        h=m.get('handle','')
        if cur is None or h in ('0x0000000000000000','0x0',0):proc_root[pid]=m.get('path','')

    # CSV outputs prepared before symbol pass.
    write_csv(out/'runtime_processes.csv',['pid','proc_address','dynlib_list_head','root_module'],[[r.get('pid',''),r.get('proc_address',''),r.get('dynlib_list_head',''),proc_root.get(r.get('pid'),'')] for r in runtime_processes])
    write_csv(out/'runtime_process_resources.csv',['pid','proc_address','thread_head','filedesc','fd_files','root_vnode','jail_vnode','vmspace','dynlib_list_head','thread_read_rc','filedesc_read_rc','vmspace_read_rc','dynlib_read_rc','policy'],[[r.get(k,'') for k in ('pid','proc_address','thread_head','filedesc','fd_files','root_vnode','jail_vnode','vmspace','dynlib_list_head','thread_read_rc','filedesc_read_rc','vmspace_read_rc','dynlib_read_rc','policy')] for r in process_resources])
    write_csv(out/'runtime_threads.csv',['pid','thread_address','tid','tid_raw','next','layout_source'],[[r.get(k,'') for k in ('pid','thread_address','tid','tid_raw','next','layout_source')] for r in runtime_threads])
    write_csv(out/'runtime_vmspaces.csv',['pid','vmspace','root_offset','vm_root','layout_source'],[[r.get(k,'') for k in ('pid','vmspace','root_offset','vm_root','layout_source')] for r in runtime_vmspaces])
    write_csv(out/'runtime_vm_regions.csv',['pid','entry_address','start','end','size','prot_raw','next'],[[r.get(k,'') for k in ('pid','entry_address','start','end','size','prot_raw','next')] for r in runtime_vmregions])
    write_csv(out/'runtime_system_anchors.csv',['kind','address','value','first_raw','read_rc','layout','raw16'],[[r.get(k,'') for k in ('kind','address','value','first_raw','read_rc','layout','raw16')] for r in system_anchors])
    write_csv(out/'runtime_fd_tables.csv',['pid','filedesc','fd_files','nfiles_raw','read_rc','entries_offset','entry_stride','layout_source'],[[r.get(k,'') for k in ('pid','filedesc','fd_files','nfiles_raw','read_rc','entries_offset','entry_stride','layout_source')] for r in fd_tables])
    write_csv(out/'runtime_fds.csv',['pid','fd','entry_address','fde_file','file_data','file_data_read_rc','layout_source','layout_confidence','policy'],[[r.get(k,'') for k in ('pid','fd','entry_address','fde_file','file_data','file_data_read_rc','layout_source','layout_confidence','policy')] for r in runtime_fds])
    write_csv(out/'runtime_fd_data_raw.csv',['pid','fd','file_data','size','read_rc','interpretation','hex'],[[r.get(k,'') for k in ('pid','fd','file_data','size','read_rc','interpretation','hex')] for r in fd_data_raw])
    write_csv(out/'runtime_fd_data_pointer_candidates.csv',['pid','fd','file_data','offset','pointer','evidence'],[[r.get(k,'') for k in ('pid','fd','file_data','offset','pointer','evidence')] for r in fd_data_ptrs])
    write_csv(out/'runtime_bus_devices.csv',['index','device_address','next','parent','driver','devclass','unit_raw','nameunit','desc','busy_raw','state_raw','devflags_raw','flags_raw','order_raw','ivars','softc','layout_confidence'],[[r.get(k,'') for k in ('index','device_address','next','parent','driver','devclass','unit_raw','nameunit','desc','busy_raw','state_raw','devflags_raw','flags_raw','order_raw','ivars','softc','layout_confidence')] for r in bus_devices])
    write_csv(out/'runtime_bus_raw_candidates.csv',['index','device_address','next_ps5_confirmed','nameunit_pointer_at_0x58','nameunit_read_rc','expected_last_device','reason','raw_prefix'],[[r.get(k,'') for k in ('index','device_address','next_ps5_confirmed','nameunit_pointer_at_0x58','nameunit_read_rc','expected_last_device','reason','raw_prefix')] for r in bus_raw_candidates])
    write_csv(out/'runtime_bus_candidate_strings.csv',['device_address','offset','pointer','value','evidence'],[[r.get(k,'') for k in ('device_address','offset','pointer','value','evidence')] for r in bus_candidate_strings])
    write_csv(out/'runtime_bus_list_heads.csv',['address','first','tail_next_slot','expected_last_device','layout_source'],[[r.get(k,'') for k in ('address','first','tail_next_slot','expected_last_device','layout_source')] for r in bus_list_heads])
    write_csv(out/'runtime_bus_list_terminators.csv',['device_address','expected_last_device','kind','matches_tailq_expected_last'],[[r.get(k,'') for k in ('device_address','expected_last_device','kind','matches_tailq_expected_last')] for r in bus_terminators])
    write_csv(out/'runtime_bus_list_consistency.csv',['head_read_rc','first_before','first_after','tail_slot_before','tail_slot_after','stable','ended_with_null'],[[r.get(k,'') for k in ('head_read_rc','first_before','first_after','tail_slot_before','tail_slot_after','stable','ended_with_null')] for r in bus_consistency])
    write_csv(out/'runtime_bus_drivers.csv',['driver_address','name','methods','class_size_raw','baseclasses','refs_raw','ops','layout_source'],[[r.get(k,'') for k in ('driver_address','name','methods','class_size_raw','baseclasses','refs_raw','ops','layout_source')] for r in bus_drivers])
    write_csv(out/'runtime_bus_driver_methods.csv',['driver_address','driver_name','index','method_address','desc','desc_id_raw','desc_id_read_rc','func','default_func_raw','kernel_text_offset','layout_source'],[[r.get(k,'') for k in ('driver_address','driver_name','index','method_address','desc','desc_id_raw','desc_id_read_rc','func','default_func_raw','kernel_text_offset','layout_source')] for r in bus_methods])
    write_csv(out/'runtime_bus_softc_heads.csv',['device_address','nameunit','softc','size','interpretation','hex'],[[r.get(k,'') for k in ('device_address','nameunit','softc','size','interpretation','hex')] for r in bus_softc])
    write_csv(out/'runtime_observable_limits.csv',['stage','reason','address','size'],[[r.get(k,'') for k in ('stage','reason','address','size')] for r in observable_limits])
    runtime_header=['pid','path','canonical_path','handle','mapbase','mapsize','textsize_raw','database_raw','datasize_raw','entry_raw','vaddrbase_raw','tlsindex_raw','pltgot_raw','init_raw','fini_raw','status_raw','flags_raw','dynsec','prefix_layout_confidence','extended_layout_sane','extended_layout_confidence']
    write_csv(out/'runtime_modules.csv',runtime_header,[[r.get('pid',''),r.get('path',''),canonical_path(r.get('path',''))]+[r.get(k,'') for k in runtime_header[3:]] for r in runtime_modules])
    write_csv(out/'static_modules.csv',['path','canonical_path','container','sha256','elf_type','machine','entry','phnum','shnum'],[[r.get('path',''),canonical_path(r.get('path','')),r.get('container',''),r.get('sha256',''),r.get('elf_type',''),r.get('machine',''),r.get('entry',''),r.get('phnum',''),r.get('shnum','')] for r in static_modules])
    write_csv(out/'candidate_headers.csv',['path','file_size','header_bytes','header_hex'],[[r.get(k,'') for k in ('path','file_size','header_bytes','header_hex')] for r in candidate_headers])
    write_csv(out/'libraries.csv',['source','pid','path','canonical_path','kind','name','object_id','version_major','version_minor','raw'],library_rows)
    write_csv(out/'device_nodes.csv',['path','kind','mode','rdev','target','category_hint'],[[d.get('path',''),d.get('kind',''),d.get('mode',''),d.get('rdev',''),d.get('target',''),category('','',d.get('path',''))] for d in devices])
    write_csv(out/'coverage_gaps.csv',['record','pid','path','stage','reason','address','size','message','errno'],[[r.get(k,'') for k in ('record','pid','path','stage','reason','address','size','message','errno')] for r in gaps])
    write_csv(out/'runtime_object_raw.csv',['pid','path','object_address','size','hex'],[[r.get(k,'') for k in ('pid','path','object_address','size','hex')] for r in raw_objects])
    write_csv(out/'runtime_dynsec_raw.csv',['pid','path','address','size','hex'],[[r.get(k,'') for k in ('pid','path','address','size','hex')] for r in raw_dynsecs])
    write_csv(out/'runtime_raw_table_inventory.csv',['pid','path','kind','base','total_size','offset','size'],[[r.get(k,'') for k in ('pid','path','kind','base','total_size','offset','size')] for r in raw_tables])
    write_csv(out/'runtime_strtab_strings.csv',['pid','path','offset','value'],[[r.get(k,'') for k in ('pid','path','offset','value')] for r in strtab_strings])
    write_csv(out/'runtime_library_attrs.csv',['pid','path','kind','object_id','attrs','raw'],[[r.get(k,'') for k in ('pid','path','kind','object_id','attrs','raw')] for r in lib_attrs])
    write_csv(out/'filesystem_dirs.csv',['path','mode','dev','ino'],[[r.get(k,'') for k in ('path','mode','dev','ino')] for r in fs_dirs])
    write_csv(out/'extension_candidates.csv',['path','file_size','reason'],[[r.get(k,'') for k in ('path','file_size','reason')] for r in ext_candidates])
    write_csv(out/'large_image_candidates.csv',['path','file_size','capture','reason'],[[r.get(k,'') for k in ('path','file_size','capture','reason')] for r in large_candidates])
    write_csv(out/'record_inventory.csv',['record','count'],counts.most_common())
    write_csv(out/'dynamic_tag_inventory.csv',['tag_name','count'],tag_counts.most_common())

    driver_by_addr={str(r.get('driver_address','')):r for r in bus_drivers}
    bus_edges=[]
    for d in bus_devices:
        drv=driver_by_addr.get(str(d.get('driver','')), {})
        bus_edges.append([d.get('device_address',''),d.get('nameunit',''),d.get('desc',''),d.get('parent',''),d.get('driver',''),drv.get('name',''),d.get('softc',''),'EXACT-KERNEL-POINTER'])
    write_csv(out/'bus_device_driver_edges.csv',['device_address','nameunit','device_desc','parent','driver_address','driver_name','softc','evidence'],bus_edges)

    device_by_addr={str(r.get('device_address','')):r for r in bus_devices}
    parent_edges=[];bus_graph=[];bus_cat=Counter()
    methods_by_driver=Counter(str(r.get('driver_address','')) for r in bus_methods)
    for d in bus_devices:
        parent=device_by_addr.get(str(d.get('parent','')), {})
        drv=driver_by_addr.get(str(d.get('driver','')), {})
        cat=category('',drv.get('name',''),' '.join([d.get('nameunit',''),d.get('desc','')]))
        bus_cat[cat]+=1
        bus_graph.append([d.get('device_address',''),d.get('nameunit',''),d.get('desc',''),d.get('parent',''),parent.get('nameunit',''),d.get('driver',''),drv.get('name',''),methods_by_driver.get(str(d.get('driver','')),0),d.get('softc',''),cat])
        if d.get('parent') not in ('','0x0','0x0000000000000000',0,None):
            parent_edges.append([d.get('parent',''),parent.get('nameunit',''),d.get('device_address',''),d.get('nameunit',''),'EXACT-KERNEL-POINTER'])
    write_csv(out/'bus_parent_child_edges.csv',['parent_address','parent_nameunit','child_address','child_nameunit','evidence'],parent_edges)
    write_csv(out/'runtime_bus_graph.csv',['device_address','nameunit','desc','parent_address','parent_nameunit','driver_address','driver_name','driver_method_count','softc','category'],bus_graph)
    write_csv(out/'hardware_bus_surface.csv',['category','bus_device_count'],bus_cat.most_common())

    # Filesystem /dev names and kernel bus names do not carry a universal public
    # one-to-one key. Keep only conservative lexical candidates and label them CANDIDATE.
    def tok(v):
        v=(v or '').lower().split('/')[-1]
        v=re.sub(r'[^a-z0-9_]+','',v)
        return re.sub(r'\d+$','',v)
    node_bus_candidates=[]
    for dn in devices:
        dt=tok(dn.get('path',''))
        if len(dt)<3:continue
        for bd in bus_devices:
            nt=tok(bd.get('nameunit',''));drv=driver_by_addr.get(str(bd.get('driver','')), {})
            drt=tok(drv.get('name',''))
            reason=''
            if nt and dt==nt:reason='device-node token == bus nameunit token'
            elif drt and dt==drt:reason='device-node token == driver token'
            elif nt and min(len(dt),len(nt))>=4 and (dt.startswith(nt) or nt.startswith(dt)):reason='device-node/bus token prefix'
            elif drt and min(len(dt),len(drt))>=4 and (dt.startswith(drt) or drt.startswith(dt)):reason='device-node/driver token prefix'
            if reason:node_bus_candidates.append([dn.get('path',''),bd.get('device_address',''),bd.get('nameunit',''),drv.get('name',''),reason,'CANDIDATE'])
    write_csv(out/'device_node_bus_candidates.csv',['device_node','bus_device_address','bus_nameunit','driver_name','reason','confidence'],node_bus_candidates)

    # Exact pointer correlation over already captured evidence. No new
    # console calls happen here.  Raw FD resource-object qwords and raw bus-node
    # qwords are matched only against addresses independently captured by the
    # mapper; field semantics are not invented when a raw offset is unknown.
    def h2i(v):
        try:
            if isinstance(v,int): return v
            return int(str(v),0)
        except Exception:return None

    decoded_bus_addr={h2i(d.get('device_address')):(d.get('nameunit',''),d) for d in bus_devices if h2i(d.get('device_address')) is not None}
    raw_bus_addr={h2i(d.get('device_address')):d for d in bus_raw_candidates if h2i(d.get('device_address')) is not None}
    driver_addr={h2i(d.get('driver_address')):d for d in bus_drivers if h2i(d.get('driver_address')) is not None}
    softc_addr={h2i(d.get('softc')):d for d in bus_devices if h2i(d.get('softc')) not in (None,0)}

    fd_pointer_matches=[]
    for r in fd_data_ptrs:
        q=h2i(r.get('pointer')); entity='';name='';evidence=''
        if q in decoded_bus_addr:
            entity='bus_device';name=decoded_bus_addr[q][0];evidence='EXACT-POINTER-MATCH'
        elif q in raw_bus_addr:
            entity='bus_raw_device';name='';evidence='EXACT-POINTER-MATCH'
        elif q in driver_addr:
            entity='bus_driver';name=driver_addr[q].get('name','');evidence='EXACT-POINTER-MATCH'
        elif q in softc_addr:
            entity='bus_softc';name=softc_addr[q].get('nameunit','');evidence='EXACT-POINTER-MATCH'
        if entity:
            fd_pointer_matches.append([r.get('pid',''),r.get('fd',''),r.get('file_data',''),r.get('offset',''),r.get('pointer',''),entity,name,evidence,'raw resource slot semantic unknown'])
    write_csv(out/'fd_resource_pointer_matches.csv',['pid','fd','file_data','raw_offset','pointer','matched_entity','matched_name','evidence','semantic_policy'],fd_pointer_matches)


    # v0.8 offline-only resource-chain joins.  These joins reuse addresses already
    # captured by the mapper; they do not add console reads or assign unknown field names.
    fd_owners=defaultdict(list)
    for r in runtime_fds:
        a=h2i(r.get('file_data'))
        if a not in (None,0): fd_owners[a].append((str(r.get('pid','')),str(r.get('fd',''))))
    fd_resource_edges=[]
    for r in fd_data_ptrs:
        q=h2i(r.get('pointer'))
        if q not in fd_owners: continue
        src=h2i(r.get('file_data'))
        owners=fd_owners[q]
        rel='SELF-REFERENCE' if src==q else 'RESOURCE-TO-RESOURCE'
        fd_resource_edges.append([
            r.get('pid',''),r.get('fd',''),r.get('file_data',''),r.get('offset',''),r.get('pointer',''),
            len(owners),';'.join(f'{p}:{fd}' for p,fd in owners[:128]),rel,
            'EXACT-POINTER-MATCH','RAW-SLOT-SEMANTICS-UNASSIGNED'])
    write_csv(out/'fd_resource_to_resource_edges.csv',
              ['source_pid','source_fd','source_file_data','raw_offset','target_file_data','target_owner_count','target_owner_examples','relation','evidence','semantic_policy'],
              fd_resource_edges)

    # Group identical bounded raw resource snapshots.  This is a byte fingerprint,
    # not a claim that the resources share a Sony/FreeBSD type name.
    import hashlib
    raw_groups=defaultdict(lambda:{'records':0,'addrs':set(),'pids':set(),'fds':set()})
    for r in fd_data_raw:
        if str(r.get('read_rc','')) not in ('0',0): continue
        hx=str(r.get('hex',''))
        if not hx: continue
        try: digest=hashlib.sha256(bytes.fromhex(hx)).hexdigest()
        except Exception: continue
        v=raw_groups[digest]; v['records']+=1
        v['addrs'].add(str(r.get('file_data',''))); v['pids'].add(str(r.get('pid',''))); v['fds'].add(f"{r.get('pid','')}:{r.get('fd','')}")
    sig_rows=[]
    for dig,v in sorted(raw_groups.items(),key=lambda kv:(-kv[1]['records'],kv[0])):
        sig_rows.append([dig,v['records'],len(v['addrs']),len(v['pids']),len(v['fds']),';'.join(sorted(v['pids'])), ';'.join(sorted(v['addrs'])[:128]), ';'.join(sorted(v['fds'])[:128]), 'EXACT-RAW-SHA256'])
    write_csv(out/'fd_resource_signature_groups.csv',
              ['sha256','raw_record_count','unique_resource_addresses','pid_count','fd_count','pids','resource_address_examples','pid_fd_examples','evidence'],sig_rows)

    # Connected components over exact resource-pointer equality only.
    adj=defaultdict(set)
    edge_meta=defaultdict(int)
    for row in fd_resource_edges:
        a=h2i(row[2]); b=h2i(row[4])
        if a in (None,0) or b in (None,0): continue
        adj[a].add(b); adj[b].add(a); edge_meta[(min(a,b),max(a,b))]+=1
    all_nodes=set(fd_owners)|set(adj)
    seen=set(); comp_rows=[]; comp_id=0
    for root in sorted(all_nodes):
        if root in seen: continue
        comp_id+=1; stack=[root]; seen.add(root); nodes=[]
        while stack:
            a=stack.pop(); nodes.append(a)
            for b in adj.get(a,()):
                if b not in seen: seen.add(b); stack.append(b)
        owners=[]; pids=set(); fdn=0
        for a in nodes:
            for pid,fd in fd_owners.get(a,[]): owners.append(f'{pid}:{fd}'); pids.add(pid); fdn+=1
        ecount=sum(v for (a,b),v in edge_meta.items() if a in set(nodes) and b in set(nodes))
        comp_rows.append([comp_id,len(nodes),ecount,len(pids),fdn,';'.join(sorted(pids)), ';'.join(f'0x{x:016x}' for x in nodes[:128]), ';'.join(sorted(owners)[:128]), 'EXACT-POINTER-CONNECTED-COMPONENT'])
    write_csv(out/'fd_resource_components.csv',
              ['component_id','resource_node_count','pointer_edge_occurrences','pid_count','fd_owner_count','pids','resource_address_examples','pid_fd_examples','evidence'],comp_rows)

    # Re-read the raw bus prefixes offline and recover exact address matches.
    # A match can tell us that a known driver/device/softc pointer occurs in the
    # node, but not what Sony calls that raw slot; keep both facts separate.
    raw_bus_matches=[]
    for r in bus_raw_candidates:
        hx=str(r.get('raw_prefix',''))
        try:b=bytes.fromhex(hx)
        except Exception:continue
        for off in range(0,len(b)-7,8):
            q=int.from_bytes(b[off:off+8],'little')
            entity='';name=''
            if q in decoded_bus_addr:
                entity='bus_device';name=decoded_bus_addr[q][0]
            elif q in driver_addr:
                entity='bus_driver';name=driver_addr[q].get('name','')
            elif q in softc_addr:
                entity='bus_softc';name=softc_addr[q].get('nameunit','')
            if entity:
                raw_bus_matches.append([r.get('index',''),r.get('device_address',''),off,f'0x{q:016x}',entity,name,'EXACT-POINTER-MATCH','FIELD-SEMANTICS-UNASSIGNED'])
    write_csv(out/'bus_raw_pointer_matches.csv',['index','raw_bus_device','raw_offset','pointer','matched_entity','matched_name','evidence','semantic_policy'],raw_bus_matches)

    # Stable resource clusters: repeated qword pointers at the same raw offset
    # are useful type fingerprints without assigning an f_ops/socket/vnode name.
    cluster=defaultdict(lambda:{'count':0,'pids':set(),'fds':set()})
    for r in fd_data_ptrs:
        key=(intish(r.get('offset')),str(r.get('pointer','')))
        v=cluster[key];v['count']+=1;v['pids'].add(str(r.get('pid','')));v['fds'].add(f"{r.get('pid','')}:{r.get('fd','')}")
    cluster_rows=[]
    for (off,ptr),v in sorted(cluster.items(),key=lambda kv:(-kv[1]['count'],kv[0][0],kv[0][1])):
        if v['count']<2:continue
        cluster_rows.append([off,ptr,v['count'],len(v['pids']),';'.join(sorted(v['pids'])),len(v['fds']),';'.join(sorted(v['fds'])[:128]),'REPEATED-RAW-POINTER-FINGERPRINT'])
    write_csv(out/'fd_resource_pointer_clusters.csv',['raw_offset','pointer','occurrences','pid_count','pids','fd_count','pid_fd_examples','evidence'],cluster_rows)

    # One graph for MM PS5 CONTROL ingestion.  Exact kernel-pointer edges remain
    # exact; lexical /dev joins remain clearly CANDIDATE and are never promoted.
    resource_graph=[]
    for row in bus_edges:
        resource_graph.append(['bus_device',row[0],'driver',row[4],row[5],'EXACT-KERNEL-POINTER'])
    for row in parent_edges:
        resource_graph.append(['bus_device',row[0],'bus_device',row[2],row[3],'EXACT-KERNEL-POINTER'])
    for r in runtime_fds:
        resource_graph.append(['process',str(r.get('pid','')),'fd',f"{r.get('pid','')}:{r.get('fd','')}",r.get('file_data',''),'EXACT-FD-TABLE'])
    for row in fd_pointer_matches:
        resource_graph.append(['fd',f'{row[0]}:{row[1]}',row[5],row[4],row[6],row[7]])
    for row in fd_resource_edges:
        resource_graph.append(['fd_resource',row[2],'fd_resource',row[4],row[7],row[8]+'; '+row[9]])
    for row in raw_bus_matches:
        resource_graph.append(['bus_raw_device',row[1],row[4],row[3],row[5],row[6]+'; '+row[7]])
    for row in node_bus_candidates:
        resource_graph.append(['device_node',row[0],'bus_device',row[1],row[2],row[5]+'; '+row[4]])
    write_csv(out/'hardware_resource_graph.csv',['source_type','source_id','target_type','target_id','target_name_or_context','evidence'],resource_graph)

    # Combined module inventory by canonical path.
    modinfo=defaultdict(lambda:{'static':0,'runtime':0,'raw':set(),'pids':set()})
    for r in static_modules:
        c=canonical_path(r.get('path',''));modinfo[c]['static']+=1;modinfo[c]['raw'].add(r.get('path',''))
    for r in runtime_modules:
        c=canonical_path(r.get('path',''));modinfo[c]['runtime']+=1;modinfo[c]['raw'].add(r.get('path',''));modinfo[c]['pids'].add(str(r.get('pid','')))
    write_csv(out/'module_inventory.csv',['canonical_path','static_instances','runtime_instances','runtime_pids','raw_paths'],[[c,v['static'],v['runtime'],';'.join(sorted(v['pids'])),';'.join(sorted(v['raw']))] for c,v in sorted(modinfo.items())])

    # Deep process graph and VM-to-module joins.  These are joins over records read
    # from the same hardware census; no additional PS5 calls are made here.
    res_by_pid={r.get('pid'):r for r in process_resources}
    thr_count=Counter(r.get('pid') for r in runtime_threads)
    vm_count=Counter(r.get('pid') for r in runtime_vmregions)
    mod_count=Counter(r.get('pid') for r in runtime_modules)
    fd_count=Counter(r.get('pid') for r in runtime_fds)
    all_pids=sorted({r.get('pid') for r in runtime_processes}|set(res_by_pid)|set(thr_count)|set(vm_count)|set(mod_count)|set(fd_count),key=lambda x:intish(x))
    process_graph=[]
    for pid in all_pids:
        rr=res_by_pid.get(pid,{})
        process_graph.append([pid,proc_root.get(pid,''),rr.get('proc_address',''),rr.get('thread_head',''),thr_count.get(pid,0),rr.get('filedesc',''),rr.get('fd_files',''),fd_count.get(pid,0),rr.get('root_vnode',''),rr.get('jail_vnode',''),rr.get('vmspace',''),vm_count.get(pid,0),rr.get('dynlib_list_head',''),mod_count.get(pid,0)])
    write_csv(out/'runtime_process_graph.csv',['pid','root_module','proc_address','thread_head','thread_count','filedesc','fd_files','open_fd_count','root_vnode','jail_vnode','vmspace','vm_region_count','dynlib_list_head','module_count'],process_graph)

    regions_by_pid=defaultdict(list)
    for r in runtime_vmregions:
        start=intish(r.get('start'));end=intish(r.get('end'))
        if end>start:regions_by_pid[r.get('pid')].append((start,end,intish(r.get('prot_raw')),r.get('entry_address','')))
    for v in regions_by_pid.values():v.sort()
    module_vm_links=[]
    for m in runtime_modules:
        pid=m.get('pid');base=intish(m.get('mapbase'));size=intish(m.get('mapsize'));end=base+size
        if not base or not size:continue
        ovs=[];covered=0;prots=set()
        for rs,re_,prot,entry in regions_by_pid.get(pid,[]):
            if re_<=base:continue
            if rs>=end:break
            ov=max(0,min(end,re_)-max(base,rs))
            if ov:
                covered+=ov;ovs.append(entry);prots.add(str(prot))
        module_vm_links.append([pid,m.get('path',''),canonical_path(m.get('path','')),m.get('handle',''),m.get('mapbase',''),size,len(ovs),covered,f'{(100.0*covered/size):.2f}' if size else '0.00',';'.join(sorted(prots,key=intish)),';'.join(ovs[:64])])
    write_csv(out/'runtime_module_vm_links.csv',['pid','module_path','canonical_module_path','handle','mapbase','mapsize','overlap_region_count','covered_bytes','coverage_pct','prot_raw_set','vm_entry_addresses'],module_vm_links)

    # SDK catalog matched to every observed canonical module.
    sdk_by_lib=defaultdict(list)
    for nid,name,lib,origin in sdk_rows:sdk_by_lib[lib].append((nid,name,origin))
    catalog=[]
    for path in sorted(observed_canon):
        seen=set()
        for lib in possible_sdk_libs(path):
            for nid,name,origin in sdk_by_lib.get(lib,[]):
                key=(lib,nid,name)
                if key in seen:continue
                seen.add(key);decls=prototypes.get(name,[]);decl=decls[0][0] if decls else '';hdr=decls[0][1] if decls else ''
                catalog.append([path,lib,nid,name,origin,decl,hdr,category(name,lib,path)])
    write_csv(out/'observed_module_known_api_catalog.csv',['observed_module','known_library','nid','api_name','name_source','prototype','header','category'],catalog)
    if sdk_rows:write_csv(out/'known_nid_catalog.csv',['nid','api_name','library','source'],sdk_rows)

    # Pass 2: stream symbols straight to disk with exact encoded provider IDs.
    fullp=out/'full_api_map.csv';unknown=Counter();known_unique=set();unknown_unique=set();all_unique=set();provider_rows=0;symbol_records=0;known_records=0;catcount=Counter();lib_stats=defaultdict(Counter);edge_counts=Counter()
    hdr=['source','pid','process_root_module','module_path','canonical_module_path','classification','nid','raw_symbol','encoded_library_id','encoded_module_id','provider_library','provider_module','resolved_name','known_library','name_source','resolution_confidence','prototype','prototype_header','category','value','runtime_address','size','symbol_index']
    with fullp.open('w',newline='',encoding='utf-8') as fo:
        w=csv.writer(fo);w.writerow(hdr)
        with mp.open('r',encoding='utf-8',errors='replace') as f:
            for line in f:
                try:s=json.loads(line)
                except Exception:continue
                k=s.get('record')
                if k not in ('symbol','runtime_symbol'):continue
                source='runtime' if k=='runtime_symbol' else 'static';symbol_records+=1
                raw=s.get('name','');nid=s.get('nid') or extract_nid(raw);pid=s.get('pid','');path=s.get('path','');cp=canonical_path(path);classification=s.get('classification','')
                libid,modid=parse_symbol_suffix(raw)
                rm=refs.get((pid,path),{})
                provider_lib='';provider_mod=''
                if libid is not None:
                    provider_lib=rm.get('import_library',{}).get(libid) or rm.get('export_library',{}).get(libid) or ''
                if modid is not None:
                    provider_mod=rm.get('import_module',{}).get(modid) or rm.get('module_info',{}).get(modid) or ''
                if provider_lib or provider_mod:provider_rows+=1
                rows=choose_resolution(db.get(nid,[]) if nid else [],provider_lib)
                if nid:all_unique.add(nid)
                if rows:
                    if nid:known_unique.add(nid)
                    for (nm,klib,origin),conf in rows:
                        known_records+=1;decls=prototypes.get(nm,[]);decl=decls[0][0] if decls else '';ph=decls[0][1] if decls else '';cat=category(nm,provider_lib or klib,cp);catcount[cat]+=1;lib_stats[provider_lib or klib or module_stem(cp)]['known_records']+=1;lib_stats[provider_lib or klib or module_stem(cp)]['symbol_records']+=1
                        w.writerow([source,pid,proc_root.get(pid,''),path,cp,classification,nid,raw,libid if libid is not None else '',modid if modid is not None else '',provider_lib,provider_mod,nm,klib,origin,conf,decl,ph,cat,s.get('value',''),s.get('runtime_address',''),s.get('size',0),s.get('index',0)])
                        if classification=='import':edge_counts[(source,str(pid),cp,provider_lib,provider_mod,nid,nm,cat)]+=1
                else:
                    if nid:unknown_unique.add(nid);unknown[(nid,provider_lib,provider_mod)]+=1
                    nm=raw if raw.startswith(('sce','Sce','_sce','__sys_','kernel_')) else '';cat=category(nm,provider_lib,cp);catcount[cat]+=1;lib_stats[provider_lib or module_stem(cp)]['symbol_records']+=1
                    w.writerow([source,pid,proc_root.get(pid,''),path,cp,classification,nid,raw,libid if libid is not None else '',modid if modid is not None else '',provider_lib,provider_mod,nm,'','','raw-only','','',cat,s.get('value',''),s.get('runtime_address',''),s.get('size',0),s.get('index',0)])
                    if classification=='import':edge_counts[(source,str(pid),cp,provider_lib,provider_mod,nid,nm or raw,cat)]+=1

    write_csv(out/'unknown_nids.csv',['nid','provider_library','provider_module','occurrences'],[[a,b,c,n] for (a,b,c),n in unknown.most_common()])
    write_csv(out/'hardware_surface.csv',['category','symbol_rows'],catcount.most_common())
    write_csv(out/'api_library_surface.csv',['library_or_module','symbol_rows','known_rows'],[[k,v['symbol_records'],v['known_records']] for k,v in sorted(lib_stats.items(),key=lambda kv:(-kv[1]['symbol_records'],kv[0]))])

    write_csv(out/'api_provider_consumer_edges.csv',['source','pid','consumer_module','provider_library','provider_module','nid','api_name_or_raw','category','occurrences'],[[*k,n] for k,n in edge_counts.most_common()])

    report=out/'REPORT.md'
    with report.open('w',encoding='utf-8') as f:
        f.write('# MM PS5 API MAP — v0.8 resource-chain correlation report\n\n')
        f.write(f'- Source map: `{mp}`\n- Tool version: `{header.get("version","?")}`\n- Firmware: `{header.get("firmware_raw","?")}`\n')
        f.write(f'- Static images: **{len(static_modules)}**\n- Runtime processes: **{len(runtime_processes)}**\n- Runtime module instances: **{len(runtime_modules)}**\n- Unique canonical modules: **{len(modinfo)}**\n')
        f.write(f'- Runtime/static symbol records streamed: **{symbol_records}**\n- Unique runtime/static NIDs: **{len(all_unique)}**\n- Known unique NIDs after local DB join: **{len(known_unique)}**\n- Unknown unique NIDs retained: **{len(unknown_unique)}**\n')
        pct=(100.0*len(known_unique)/len(all_unique)) if all_unique else 0.0
        f.write(f'- Name coverage of observed unique NIDs: **{pct:.2f}%**\n- Symbol records with encoded provider library/module resolved: **{provider_rows}**\n')
        f.write(f'- Known-name catalog records: **{len(sdk_rows)}**\n- Known APIs matched to observed module names: **{len(catalog)}**\n- SDK prototype declarations indexed: **{sum(len(v) for v in prototypes.values())}**\n')
        f.write(f'- Runtime relocations: **{runtime_relocs}**; static relocations: **{static_relocs}**\n- Device entries: **{len(devices)}**\n- Coverage/error records: **{len(gaps)}**\n- Runtime process-resource records: **{len(process_resources)}**\n- Runtime threads: **{len(runtime_threads)}**\n- Runtime VM spaces: **{len(runtime_vmspaces)}**\n- Runtime VM regions: **{len(runtime_vmregions)}**\n- Runtime module-to-VM joins: **{len(module_vm_links)}**\n- API provider-consumer edges: **{len(edge_counts)}**\n- System anchors: **{len(system_anchors)}**\n- FD tables: **{len(fd_tables)}**; occupied FD records: **{len(runtime_fds)}**; raw FD resource objects: **{len(fd_data_raw)}**; raw FD pointer candidates: **{len(fd_data_ptrs)}**\n- Kernel bus decoded devices: **{len(bus_devices)}**; raw continuation candidates: **{len(bus_raw_candidates)}**; candidate strings: **{len(bus_candidate_strings)}**; drivers: **{len(bus_drivers)}**; driver methods: **{len(bus_methods)}**\n- Bus softc raw heads: **{len(bus_softc)}**; list-head records: **{len(bus_list_heads)}**; terminators: **{len(bus_terminators)}**; consistency records: **{len(bus_consistency)}**; observable-limit records: **{len(observable_limits)}**\n- Device-node ↔ bus lexical candidates: **{len(node_bus_candidates)}**\n- FD resource exact-pointer matches to bus entities: **{len(fd_pointer_matches)}**; exact resource-to-resource pointer edges: **{len(fd_resource_edges)}**; raw-bus exact-pointer matches: **{len(raw_bus_matches)}**; repeated FD pointer clusters: **{len(cluster_rows)}**; raw resource signature groups: **{len(sig_rows)}**; exact resource connected components: **{len(comp_rows)}**\n- Unified hardware resource graph edges: **{len(resource_graph)}**\n- Exact bus parent-child edges: **{len(parent_edges)}**\n- Raw runtime dynlib objects preserved: **{len(raw_objects)}**\n- Raw runtime dynsec structures preserved: **{len(raw_dynsecs)}**\n- Runtime loader raw-table chunks preserved: **{len(raw_tables)}**\n- Runtime strtab strings inventoried: **{len(strtab_strings)}**\n- Runtime library-attribute records: **{len(lib_attrs)}**\n- Filesystem directories inventoried: **{len(fs_dirs)}**\n- Extension candidates retained: **{len(ext_candidates)}**\n\n')
        f.write('## Evidence semantics\n\n')
        f.write('- `runtime`: read from loader/process structures already present on the console.\n- `runtime_thread` and `runtime_vm_region`: offsets are source-grounded to ps5-payload-dev/sdk `kernel_get_proc_thread`, `kernel_get_vmem_entry`, and `kernel_get_vmem_protection`; the mapper only reads.\n- `runtime_process_resources`: filedesc/root/jail/vmspace/thread/dynlib anchors; credential structures are intentionally excluded.\n- `runtime_fd_table/runtime_fd`: descriptor-table slots follow the exact public PS5 SDK `kernel_get_proc_file` entry formula; struct-file credential internals are not decoded.\n- `runtime_bus_device`: global-next/nameunit/softc offsets are PS5-source-grounded by the public cragson A53 PoC; the rest of the decoded prefix is FreeBSD-lineage and runtime-string validated.\n- `runtime_bus_device_raw_candidate`: when nameunit validation fails, no semantic decode is invented; the raw 0x90-byte prefix is retained and traversal continues only through the PS5-confirmed +0x18 global-next pointer. `runtime_bus_list_head` records the TAILQ first/tail-slot evidence and `runtime_bus_list_consistency` re-reads the head after the walk.\n- `runtime_bus_driver_method`: bounded FreeBSD kobj method-table decode; function/descriptor addresses are evidence, not invoked APIs.\n- `device_node_bus_candidates.csv`: heuristic PC-side lexical candidates only; never treated as exact hardware binding.\n- `runtime_module_vm_links.csv` and `api_provider_consumer_edges.csv`: PC-side joins over captured evidence, not extra hardware calls.\n- `static`: parsed from filesystem ELF/SELF metadata.\n- `known_nid_catalog.csv`: names from local SDK/aerolib inputs, never invented.\n- `provider_library/provider_module`: decoded from the Sony symbol suffix object IDs and matched against that module\'s dynamic library-reference table.\n- `UNVERIFIED-FW-LAYOUT`: raw dynlib object bytes are retained because some extended fields do not pass firmware-layout sanity checks.\n- Unknown NIDs remain unknown. A literal 100% human-name/signature claim is not made when Sony metadata is stripped/encrypted or no public name exists.\n\n')
        f.write('## Hardware/API domains\n\n')
        for k,v in catcount.most_common():f.write(f'- {k}: {v}\n')
    manifest={
      'tool':header.get('tool'),'version':header.get('version'),'firmware':header.get('firmware_raw'),
      'static_images':len(static_modules),'runtime_processes':len(runtime_processes),'runtime_modules':len(runtime_modules),'canonical_modules':len(modinfo),
      'symbol_records':symbol_records,'unique_nids':len(all_unique),'known_unique_nids':len(known_unique),'unknown_unique_nids':len(unknown_unique),
      'provider_resolved_symbol_records':provider_rows,'known_catalog_records':len(sdk_rows),'observed_module_known_api_rows':len(catalog),
      'prototype_records':sum(len(v) for v in prototypes.values()),'device_entries':len(devices),'coverage_records':len(gaps),
      'runtime_relocations':runtime_relocs,'static_relocations':static_relocs,'runtime_process_resources':len(process_resources),'runtime_threads':len(runtime_threads),'runtime_vmspaces':len(runtime_vmspaces),'runtime_vm_regions':len(runtime_vmregions),'runtime_module_vm_links':len(module_vm_links),'api_provider_consumer_edges':len(edge_counts),'runtime_system_anchors':len(system_anchors),'runtime_fd_tables':len(fd_tables),'runtime_fds':len(runtime_fds),'runtime_fd_data_raw':len(fd_data_raw),'runtime_fd_data_pointer_candidates':len(fd_data_ptrs),'fd_resource_pointer_matches':len(fd_pointer_matches),'fd_resource_to_resource_edges':len(fd_resource_edges),'fd_resource_signature_groups':len(sig_rows),'fd_resource_components':len(comp_rows),'bus_raw_pointer_matches':len(raw_bus_matches),'fd_resource_pointer_clusters':len(cluster_rows),'hardware_resource_graph_edges':len(resource_graph),'runtime_bus_devices':len(bus_devices),'runtime_bus_raw_candidates':len(bus_raw_candidates),'runtime_bus_candidate_strings':len(bus_candidate_strings),'runtime_bus_list_heads':len(bus_list_heads),'runtime_bus_list_terminators':len(bus_terminators),'runtime_bus_list_consistency':len(bus_consistency),'runtime_bus_drivers':len(bus_drivers),'runtime_bus_driver_methods':len(bus_methods),'runtime_bus_softc_heads':len(bus_softc),'runtime_observable_limits':len(observable_limits),'device_node_bus_candidates':len(node_bus_candidates),'bus_parent_child_edges':len(parent_edges),'runtime_raw_table_chunks':len(raw_tables),'runtime_strtab_strings':len(strtab_strings),'runtime_library_attrs':len(lib_attrs),'filesystem_dirs':len(fs_dirs),'extension_candidates':len(ext_candidates),'large_image_candidates':len(large_candidates),'record_counts':dict(counts),'summary':summary
    }
    (out/'MANIFEST.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print(f'[OK] v0.8 resource-chain correlation graph resolved map -> {out}')
    print(f'[COUNTS] symbols={symbol_records} unique_nids={len(all_unique)} known={len(known_unique)} unknown={len(unknown_unique)} provider_resolved={provider_rows}')

if __name__=='__main__':main()

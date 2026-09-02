#!/usr/bin/env python3
import argparse, csv, hashlib, json, re, struct
from pathlib import Path

CHARSET='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-'
SUFFIX=bytes.fromhex('518D64A635DED8C1E6B039B1C3E55230')
GLOBAL_RE=re.compile(r'\.global\s+([A-Za-z_.$][A-Za-z0-9_.$@]*)')
SHT_SYMTAB=2
SHT_DYNSYM=11
SHN_UNDEF=0
STT_NOTYPE=0
STT_FUNC=2

def encode_nid(name:str)->str:
    d=hashlib.sha1(name.encode('ascii')+SUFFIX).digest()[:8]
    enc=struct.unpack('<Q',d)[0]
    out=CHARSET[(enc&0xF)<<2]
    x=enc>>4
    while x:
        out+=CHARSET[x&0x3F]
        x>>=6
    return out[::-1]

def _cstr(data:bytes, off:int)->str:
    if off < 0 or off >= len(data):
        return ''
    end=data.find(b'\0', off)
    if end < 0:
        end=len(data)
    return data[off:end].decode('utf-8','ignore')

def elf_defined_symbols(path:Path):
    """Return defined function-ish symbols from an installed ELF stub .so.

    The installed Payload SDK normally keeps generated stub shared objects under
    target/lib even when the source-only sce_stubs directory is absent.
    """
    try:
        data=path.read_bytes()
    except OSError:
        return []
    if len(data) < 64 or data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
        return []
    try:
        e_shoff=struct.unpack_from('<Q',data,0x28)[0]
        e_shentsize=struct.unpack_from('<H',data,0x3A)[0]
        e_shnum=struct.unpack_from('<H',data,0x3C)[0]
    except struct.error:
        return []
    if not e_shoff or e_shentsize < 64 or not e_shnum:
        return []
    if e_shoff + e_shentsize*e_shnum > len(data):
        return []
    sections=[]
    for i in range(e_shnum):
        off=e_shoff+i*e_shentsize
        try:
            sh_type=struct.unpack_from('<I',data,off+4)[0]
            sh_offset=struct.unpack_from('<Q',data,off+0x18)[0]
            sh_size=struct.unpack_from('<Q',data,off+0x20)[0]
            sh_link=struct.unpack_from('<I',data,off+0x28)[0]
            sh_entsize=struct.unpack_from('<Q',data,off+0x38)[0]
        except struct.error:
            return []
        sections.append((sh_type,sh_offset,sh_size,sh_link,sh_entsize))
    names=[]
    for sh_type,sh_offset,sh_size,sh_link,sh_entsize in sections:
        if sh_type not in (SHT_DYNSYM,SHT_SYMTAB) or sh_entsize < 24:
            continue
        if sh_link >= len(sections):
            continue
        _,str_off,str_size,_,_=sections[sh_link]
        if str_off+str_size > len(data) or sh_offset+sh_size > len(data):
            continue
        strtab=data[str_off:str_off+str_size]
        count=sh_size//sh_entsize
        for i in range(count):
            off=sh_offset+i*sh_entsize
            try:
                st_name=struct.unpack_from('<I',data,off)[0]
                st_info=data[off+4]
                st_shndx=struct.unpack_from('<H',data,off+6)[0]
            except (struct.error,IndexError):
                break
            if st_name==0 or st_shndx==SHN_UNDEF:
                continue
            typ=st_info & 0x0f
            if typ not in (STT_NOTYPE,STT_FUNC):
                continue
            name=_cstr(strtab,st_name)
            if name and not name.startswith(('_init','_fini')):
                names.append(name)
    return names

def source_stub_rows(sdk:Path):
    stubdir=sdk/'sce_stubs'
    if not stubdir.is_dir():
        return [], None
    rows=[]
    for p in sorted(stubdir.glob('*.c')):
        lib=p.stem
        text=p.read_text(errors='ignore')
        for name in GLOBAL_RE.findall(text):
            rows.append((name,lib,'sce_stubs-source'))
    return rows, stubdir

def installed_stub_rows(sdk:Path):
    candidates=[sdk/'target'/'lib', sdk/'lib']
    rows=[]
    used=[]
    for libdir in candidates:
        if not libdir.is_dir():
            continue
        used.append(libdir)
        for p in sorted(libdir.glob('*.so')):
            lib=p.stem
            for name in elf_defined_symbols(p):
                rows.append((name,lib,'installed-stub-so'))
    return rows, used

def aerolib_rows(path:Path):
    rows=[]
    if not path or not path.is_file():
        return rows
    try:
        with path.open('r',encoding='utf-8',errors='replace') as f:
            for line in f:
                line=line.strip()
                if not line or line.startswith('#'):
                    continue
                parts=line.split(None,1)
                if len(parts)!=2:
                    continue
                nid,name=parts[0].strip(),parts[1].strip()
                if len(nid)!=11 or not name:
                    continue
                rows.append((nid,name,'','aerolib-local'))
    except OSError:
        return []
    return rows

def build_rows(sdk:Path, extra_aerolib:Path=None):
    raw,src=source_stub_rows(sdk)
    sources=[]
    if src:
        sources.append(str(src))
    if not raw:
        raw,used=installed_stub_rows(sdk)
        sources.extend(str(x) for x in used)
    seen=set(); rows=[]
    for name,lib,origin in raw:
        key=(name,lib)
        if key in seen:
            continue
        seen.add(key)
        try:
            nid=encode_nid(name)
        except UnicodeEncodeError:
            continue
        rows.append((nid,name,lib,origin))

    aerocandidates=[]
    if extra_aerolib:
        aerocandidates.append(extra_aerolib)
    aerocandidates.extend([sdk/'sce_stubs'/'aerolib.csv',sdk/'aerolib.csv',sdk/'share'/'aerolib.csv'])
    used_aero=set()
    for apath in aerocandidates:
        if not apath or not apath.is_file() or str(apath) in used_aero:
            continue
        used_aero.add(str(apath)); sources.append(str(apath))
        for nid,name,lib,origin in aerolib_rows(apath):
            key=(name,lib)
            if key in seen:
                continue
            seen.add(key)
            rows.append((nid,name,lib,origin))
    return rows,sources

def main():
    ap=argparse.ArgumentParser(description='Build NID/name database from a PS5 Payload SDK install or source tree')
    ap.add_argument('--sdk', required=True, help='PS5_PAYLOAD_SDK path')
    ap.add_argument('--out', default='resolved/sdk_api_db.csv')
    ap.add_argument('--extra-aerolib', default=None, help='optional local aerolib.csv for broader NID/name coverage')
    args=ap.parse_args()
    sdk=Path(args.sdk)
    rows,sources=build_rows(sdk,Path(args.extra_aerolib) if args.extra_aerolib else None)
    out=Path(args.out); out.parent.mkdir(parents=True,exist_ok=True)
    with out.open('w',newline='',encoding='utf-8') as f:
        w=csv.writer(f)
        w.writerow(['nid','name','sdk_stub_library','source'])
        w.writerows(rows)
    jout=out.with_suffix('.json')
    jout.write_text(json.dumps([{'nid':a,'name':b,'library':c,'source':d} for a,b,c,d in rows],indent=2),encoding='utf-8')
    if rows:
        print(f'[OK] {len(rows)} SDK API names -> {out}')
        print('[SDK-DB] source=' + (';'.join(sources) if sources else 'unknown'))
    else:
        # The API map itself does not require a name database. Keep BUILD alive and
        # leave a valid empty DB; raw NIDs can be resolved later from another SDK tree.
        print(f'[WARN] No SDK stub source/shared objects found below {sdk}.')
        print(f'[WARN] Wrote an empty name DB to {out}; payload build will continue.')

if __name__=='__main__':
    main()

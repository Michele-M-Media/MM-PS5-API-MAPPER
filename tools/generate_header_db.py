#!/usr/bin/env python3
"""Build a source-grounded API prototype index from local SDK headers.

This intentionally records declarations rather than inventing signatures.  A
row is emitted only when a function name already present in sdk_api_db.csv is
found in an SDK header declaration.
"""
import argparse,csv,re
from pathlib import Path

BLOCK=re.compile(r'/\*.*?\*/',re.S)
LINE=re.compile(r'//[^\n]*')
IDENT_CALL=re.compile(r'([A-Za-z_][A-Za-z0-9_]*)\s*\(')

def clean(text:str)->str:
    text=BLOCK.sub(' ',text)
    text=LINE.sub(' ',text)
    return text

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--sdk',required=True)
    ap.add_argument('--api-db',required=True)
    ap.add_argument('--out',default='output/sdk_api_prototypes.csv')
    args=ap.parse_args()
    sdk=Path(args.sdk); db=Path(args.api_db); out=Path(args.out)
    names=set()
    with db.open('r',encoding='utf-8',errors='replace',newline='') as f:
        for r in csv.DictReader(f):
            n=r.get('name') or r.get('api_name')
            if n: names.add(n)
    roots=[sdk/'include',sdk/'target'/'include']
    seen=set(); rows=[]
    for root in roots:
        if not root.is_dir(): continue
        for hp in sorted(root.rglob('*.h')):
            try: text=clean(hp.read_text(encoding='utf-8',errors='replace'))
            except OSError: continue
            # Semicolon termination handles multi-line ordinary declarations.
            for chunk in text.split(';')[:-1]:
                if '(' not in chunk or ')' not in chunk: continue
                decl=' '.join(chunk.split())+';'
                if len(decl)>4096 or decl.startswith('typedef '): continue
                toks=IDENT_CALL.findall(decl)
                if not toks: continue
                # The API function is normally the last call-like identifier in
                # a declaration; verify against the already-known API set.
                candidates=[t for t in toks if t in names]
                if not candidates: continue
                name=candidates[-1]
                key=(name,decl,str(hp.relative_to(sdk)))
                if key in seen: continue
                seen.add(key); rows.append(key)
    out.parent.mkdir(parents=True,exist_ok=True)
    with out.open('w',newline='',encoding='utf-8') as f:
        w=csv.writer(f); w.writerow(['api_name','declaration','header']); w.writerows(rows)
    print(f'[OK] {len(rows)} source-grounded SDK prototypes -> {out}')

if __name__=='__main__': main()

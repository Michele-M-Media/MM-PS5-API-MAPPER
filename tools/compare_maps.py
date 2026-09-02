#!/usr/bin/env python3
import argparse,csv
from pathlib import Path

def load(p):
    rows=list(csv.DictReader(open(p,encoding='utf-8')))
    by={}
    for r in rows:
        key=(r.get('nid') or '',r.get('resolved_name') or r.get('raw_symbol') or '',r.get('classification') or '')
        by.setdefault(key,set()).add(r.get('module_path',''))
    return by

def main():
    ap=argparse.ArgumentParser();ap.add_argument('base');ap.add_argument('new');ap.add_argument('--out',default='MAP_DIFF.md');a=ap.parse_args()
    A=load(a.base);B=load(a.new);ka=set(A);kb=set(B)
    added=sorted(kb-ka);removed=sorted(ka-kb);moved=[]
    for k in sorted(ka&kb):
        if A[k]!=B[k]:moved.append((k,A[k],B[k]))
    o=Path(a.out)
    with o.open('w',encoding='utf-8') as f:
        f.write('# MM PS5 API map diff\n\n')
        f.write(f'- Added API records: **{len(added)}**\n- Removed API records: **{len(removed)}**\n- Provider/module-set changes: **{len(moved)}**\n\n')
        f.write('## Added\n\n');
        for k in added:f.write(f'- `{k[0]}` {k[1]} [{k[2]}]\n')
        f.write('\n## Removed\n\n');
        for k in removed:f.write(f'- `{k[0]}` {k[1]} [{k[2]}]\n')
        f.write('\n## Provider/module changes\n\n')
        for k,x,y in moved:f.write(f'- `{k[0]}` {k[1]}: `{sorted(x)}` -> `{sorted(y)}`\n')
    print(f'[OK] {o}')
if __name__=='__main__':main()

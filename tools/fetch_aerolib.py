#!/usr/bin/env python3
"""Best-effort fetch of the public aerolib NID/name database.

The mapper does not depend on network access.  Failure is non-fatal and leaves
SDK-local name resolution in place.
"""
import argparse
from pathlib import Path
from urllib.request import Request, urlopen

URL='https://raw.githubusercontent.com/zecoxao/sce_symbols/main/aerolib.csv'

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--out',default='output/aerolib.csv')
    ap.add_argument('--timeout',type=float,default=12.0)
    args=ap.parse_args()
    out=Path(args.out); out.parent.mkdir(parents=True,exist_ok=True)
    try:
        req=Request(URL,headers={'User-Agent':'MM-PS5-API-MAPPER/0.3'})
        with urlopen(req,timeout=args.timeout) as r:
            data=r.read()
        if len(data)<1024 or b' ' not in data[:4096]:
            raise RuntimeError('download did not look like aerolib.csv')
        out.write_bytes(data)
        print(f'[OK] aerolib snapshot: {len(data)} bytes -> {out}')
        return 0
    except Exception as e:
        print(f'[WARN] aerolib fetch skipped: {e}')
        return 0

if __name__=='__main__': raise SystemExit(main())

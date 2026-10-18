#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Deterministic source snapshot with hashes; excludes generated audio/user data.
This is not an installable firmware package and never accesses a device.
"""
import gzip,hashlib,io,json,tarfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
files=[]
for name in ['firmware','host','tests','scripts','docs','web','third_party']:
    files += [p for p in (ROOT/name).rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc']
for name in ['README.md','LICENSE','THIRD_PARTY_NOTICES.md','CHANGELOG.md','AGENTS.md','FM1_FZ_INSPIRED_SPEC.md','Makefile','.gitignore','.clang-format']:
    files.append(ROOT/name)
manifest={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(files)}
out=ROOT/'build/fm1-workstation-source.tar.gz';out.parent.mkdir(exist_ok=True)
with out.open('wb') as raw,gzip.GzipFile(filename='',mode='wb',fileobj=raw,mtime=0) as compressed,tarfile.open(fileobj=compressed,mode='w|') as tar:
    for name,digest in manifest.items():
        path=ROOT/name;payload=path.read_bytes();entry=tarfile.TarInfo('fm1-workstation/'+name)
        entry.size=len(payload);entry.mode=0o755 if path.stat().st_mode&0o111 else 0o644
        tar.addfile(entry,io.BytesIO(payload))
    payload=(json.dumps(manifest,sort_keys=True,indent=2)+'\n').encode();entry=tarfile.TarInfo('fm1-workstation/SOURCE_SHA256.json');entry.size=len(payload);entry.mode=0o644;tar.addfile(entry,io.BytesIO(payload))
print(str(out.relative_to(ROOT)),hashlib.sha256(out.read_bytes()).hexdigest(),f'({len(files)} source files; no device image)')

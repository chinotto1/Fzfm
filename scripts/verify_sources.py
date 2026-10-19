#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify exact bytes of the vendored BSP. Does not fetch anything."""
import hashlib
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
lock=json.loads((ROOT/'third_party/felucca-bsp.lock.json').read_text())
base=ROOT/lock['path']
expected=lock['sha256']
actual={p.relative_to(base).as_posix() for p in base.rglob('*') if p.is_file()}
if actual!=set(expected): raise SystemExit('BSP file set does not match lock')
for name,digest in expected.items():
    if hashlib.sha256((base/name).read_bytes()).hexdigest()!=digest:raise SystemExit('BSP hash mismatch: '+name)
print(f'Vendored BSP: {len(expected)} exact file hashes verified')
installer=json.loads((ROOT/'third_party/fm1-installer.lock.json').read_text())
for name,entry in installer['files'].items():
    if hashlib.sha256((ROOT/entry['local']).read_bytes()).hexdigest()!=entry['local_sha256']:
        raise SystemExit('Installer source hash mismatch: '+name)
print(f"Installer protocol: {len(installer['files'])} modified source hashes verified")

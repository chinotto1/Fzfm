#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
make benchmark
python3 - <<'PY'
import json
from pathlib import Path
p=Path('build/target/sampler/manifest.json')
if p.exists():
    m=json.loads(p.read_text())
    print('Linked development sampler:',m['bytes'],'bytes, SHA256',m['sha256'])
    print('Linked sections:',m.get('section_bytes',{}))
    print('Device measured:',m['device_tested'],'Installable:',m['installable_package'])
else: print('No target manifest in this checkout; see docs/BUILD.md.')
print('PCM16: 72000 mono bytes/second; 144000 stereo. No flash sample budget is claimed.')
PY

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile actual sampler or reproduce safe BSP baseline; never access USB devices.

Requires JieLi Linux x86-64 compiler. Sampler also needs externally retained
Newlib/compiler-rt SDK runtime (no native engine), with locked hashes/notices.
A successful link would be an unqualified development image, never a release.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
BSP = ROOT / 'third_party/felucca-bsp'
RUNTIME_HASHES = {
    'lib/libc.a': 'fcea6ec66162a5173db4626f4c98d63c2873c80ac48c849efbe0fd223fe94a58',
    'lib/libcompiler_rt.a': 'c2116a5e2557bbfd1181a5f4db28d271f2d3df3b47743ab1d4311323c8f8e251',
}


def run(command):
    print('+', ' '.join(str(a) for a in command), flush=True)
    subprocess.run([str(a) for a in command], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', action='store_true', help='BSP hwtest CPU0 safe image only')
    parser.add_argument('--objects-only', action='store_true', help='compile actual source without claiming a linked image')
    args = parser.parse_args()
    tc = Path(os.environ.get('JIELI_TOOLCHAIN', '/nonexistent'))
    cc, ld, objcopy, objdump = (tc / p for p in (
        'pi32v2/bin/clang', 'pi32v2/bin/ld', 'common/bin/objcopy', 'common/bin/objdump'))
    if not cc.is_file():
        sys.exit('TARGET BUILD BLOCKED: set JIELI_TOOLCHAIN to the separately acquired JieLi Linux '
                 'PI32v2 clang 4.0.1 toolchain. No target image was produced. '
                 ''
                 'See docs/HARDWARE_AUDIT.md; host compiler is not a PI32v2 substitute.')
    def verify(base, filename):
        lock = json.loads((ROOT / filename).read_text())
        for name, expected in lock['sha256'].items():
            path = base / name
            if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                sys.exit('TARGET BUILD BLOCKED: pinned dependency mismatch: ' + str(path))
    verify(tc, 'third_party/toolchain.lock.json')
    verify(BSP, 'third_party/felucca-bsp.lock.json')
    out = ROOT / 'build/target' / ('baseline' if args.baseline else 'sampler')
    out.mkdir(parents=True, exist_ok=True)
    flags = ['-target', 'pi32v2', '-Os', '-ffunction-sections', '-fdata-sections',
             '-fno-builtin', '-Wall', '-Wextra', '-Wno-unused-function',
             '-I' + str(BSP / 'chip'), '-I' + str(BSP / 'boards/fm1')]
    runtime = None
    if args.baseline:
        flags += ['-DHWTEST_EXPERIMENTAL=0']
        sources = [BSP / 'examples/hwtest/hwtest.c']
        script = BSP / 'examples/hwtest/app.ld'
    else:
        runtime = Path(os.environ.get('FM1_RUNTIME_DIR', '/nonexistent'))
        verify(runtime, 'third_party/runtime.lock.json')
        for filename, expected in RUNTIME_HASHES.items():
            path = runtime / filename
            if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                sys.exit('TARGET BUILD BLOCKED: FM1_RUNTIME_DIR must point to the pinned official '
                         'fm1_open SDK runtime (Newlib/compiler-rt only); missing/hash mismatch: ' + filename)
        if not (runtime / 'include/libc/stdlib.h').is_file():
            sys.exit('TARGET BUILD BLOCKED: pinned Newlib headers missing')
        flags += ['-std=gnu11', '-mcpu=r3', '-mfprev1', '-DFM_UNDO_DEPTH=2',
                  '-DFM_UNDO_PATCHES=24', '-DFM_OBJECT_LIMIT=64', '-DFM_ENGINE_EVENTS=128',
                  '-DFM_DELAY_FRAMES=4098', '-DFM_SEQUENCE_HELD_LIMIT=64', '-DFM_SRC_PHASE_LIMIT=64', '-I' + str(ROOT / 'firmware/workstation'),
                  '-I' + str(runtime / 'include/libc')]
        sources = sorted((ROOT / 'firmware/workstation').rglob('*.c'))
        sources = [p for p in sources if p.name != 'journal.c']
        script = ROOT / 'firmware/workstation/platform/fm1/app.ld'
    objects = []
    for index, source in enumerate([BSP / 'chip/crt0.S', BSP / 'chip/fm1_vec.S',
                                    BSP / 'chip/fm1_isr.S', *sources]):
        dest = out / ('%02d_%s.o' % (index, source.stem))
        run([cc, *flags, '-c', source, '-o', dest])
        objects.append(dest)
    if args.objects_only:
        print('Target source objects compiled; no linked application or package exists.')
        return
    elf, mapfile = out / 'app.elf', out / 'app.map'
    link = [ld, '-T', script, '--gc-sections', '-Map=' + str(mapfile), *objects]
    if runtime:
        link += ['--start-group', runtime / 'lib/libc.a', tc / 'pi32v2/lib/libg.a', tc / 'pi32v2/lib/libm.a',
                 runtime / 'lib/libcompiler_rt.a', '--end-group']
    run([*link, '-o', elf])
    symbols = subprocess.check_output([str(objdump), '-t', str(elf)], text=True)
    def symbol(name):
        match = re.search(r'^([0-9a-f]+) .*\s' + re.escape(name) + r'$', symbols, re.M)
        if not match:
            sys.exit('Missing required symbol ' + name)
        return int(match.group(1), 16)
    if symbol('_start') != 0x02000120:
        sys.exit('Rejected app: unexpected XIP entry')
    image = bytearray()
    for section, start in [('.text', 0x02000120), ('.ram_text', symbol('_rt_load')),
                           ('.data', symbol('_data_load'))]:
        path = out / (section[1:] + '.bin')
        run([objcopy, '-O', 'binary', '-j', section, elf, path])
        payload = path.read_bytes()
        if not payload:
            continue
        offset = start - 0x02000120
        if offset < len(image):
            sys.exit('Rejected app: overlapping section load images')
        image += b'\xff' * (offset - len(image)) + payload
    image += b'\xff' * (-len(image) % 4)
    if len(image) > 0x8DFBC:
        sys.exit('Rejected app: exceeds conservative XIP app window')
    binary = out / 'app.bin'
    binary.write_bytes(image)
    source_paths = ([*sources, script, BSP / 'chip/crt0.S', BSP / 'chip/fm1_vec.S', BSP / 'chip/fm1_isr.S', Path(__file__).resolve()] if args.baseline else [path for path in sorted((ROOT / 'firmware/workstation').rglob('*')) if path.is_file()])
    metadata = {'kind': 'safe BSP baseline' if args.baseline else 'UNQUALIFIED sampler development image',
                'sha256': hashlib.sha256(image).hexdigest(), 'bytes': len(image),
                'compiler': subprocess.check_output([str(cc), '--version'], text=True),
                'flags': flags, 'device_tested': False, 'installable_package': False,
                'bsp': json.loads((ROOT / 'third_party/felucca-bsp.lock.json').read_text()),
                'dependencies': {name: json.loads((ROOT / ('third_party/' + name + '.lock.json')).read_text()) for name in ['toolchain', 'runtime'] if runtime or name == 'toolchain'},
                'source_sha256': {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in source_paths},
                'section_bytes': {a: symbol(b) - symbol(c) for a,b,c in [('pool','_pool_end','_pool_start'), ('bss','_bss_end','_bss_start'), ('data','_data_end','_data_start')] if not args.baseline or a != 'pool'},
                'known_limitations': ['no qualified sampler flash persistence',
                    'no qualified iPad USB/codec path', 'no device recovery test',
                    'bring-up capture 4096 frames; delay limited to 113.78 ms']}
    (out / 'manifest.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print('Built and range-checked raw development image; no package/flash operation performed.')


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)

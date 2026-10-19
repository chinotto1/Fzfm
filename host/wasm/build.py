#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a self-contained static emulator; never publishes or opens a device."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out',type=Path,default=ROOT/'build/site')
args=parser.parse_args()
out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
emcc=os.environ.get('EMCC','emcc')
sources=sorted((ROOT/'firmware/workstation').glob('*/*.c'))
exports=['create','destroy','command','render','pixels','palette','text','metric','import_wave','backup','free','restore','demo','persistence_error','editor','action','parameter','draw','input','harmonics','sequence','notes','sampling','performance']
command=[emcc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function',
 '-I'+str(ROOT/'firmware/workstation'),str(ROOT/'host/simulator/bridge.c'),
 *map(str,sources),'-lm','--no-entry','-lidbfs.js',
 '-sMODULARIZE=1','-sEXPORT_ES6=1','-sENVIRONMENT=web,worker',
 '-sALLOW_MEMORY_GROWTH=1','-sINITIAL_MEMORY=67108864','-sMAXIMUM_MEMORY=268435456',
 '-sSTACK_SIZE=2097152','-sFILESYSTEM=1','-sASSERTIONS=0',
 '-sEXPORTED_FUNCTIONS='+json.dumps(['_fm_sim_'+n for n in exports]+['_malloc','_free']),
 '-sEXPORTED_RUNTIME_METHODS='+json.dumps(['FS','IDBFS','UTF8ToString','ccall']),
 '-o',str(out/'fm1-core.js')]
print('Building the portable C application for WebAssembly',flush=True)
subprocess.run(command,cwd=ROOT,check=True)
# Content-address assets so Safari/CDN caches cannot mix engine revisions.
def publish(name,content):
 digest=hashlib.sha256(content).hexdigest()[:12]
 path=Path(name);versioned=path.stem+'.'+digest+path.suffix
 (out/versioned).write_bytes(content)
 return versioned
wasm=(out/'fm1-core.wasm').read_bytes()
wasm_name=publish('fm1-core.wasm',wasm)
core_js=(out/'fm1-core.js').read_text().replace('fm1-core.wasm',wasm_name)
core_name=publish('fm1-core.js',core_js.encode())
worker=(ROOT/'web/simulator/emulator-worker.js').read_text().replace('./fm1-core.js','./'+core_name)
worker_name=publish('emulator-worker.js',worker.encode())
backend=(ROOT/'web/simulator/backend.js').read_text().replace('./emulator-worker.js','./'+worker_name)
backend_name=publish('backend.js',backend.encode())
performance_name=publish('performance.js',(ROOT/'web/simulator/performance.js').read_bytes())
sequencer_name=publish('sequencer.js',(ROOT/'web/simulator/sequencer.js').read_bytes())
sampler_name=publish('sampler.js',(ROOT/'web/simulator/sampler.js').read_bytes())
workspace=(ROOT/'web/simulator/workspace.js').read_text().replace('./sequencer.js','./'+sequencer_name).replace('./sampler.js','./'+sampler_name).replace('./performance.js','./'+performance_name)
workspace_name=publish('workspace.js',workspace.encode())
app=(ROOT/'web/simulator/app.js').read_text().replace('./backend.js','./'+backend_name)
app=app.replace('./workspace.js','./'+workspace_name)
app_name=publish('app.js',app.encode())
css_name=publish('style.css',(ROOT/'web/simulator/style.css').read_bytes())
index=(ROOT/'web/simulator/index.html').read_text().replace('data-runtime="native"','data-runtime="wasm"')
index=index.replace('NATIVE CORE · PANEL SIMULATOR','INSTRUMENT · WEB EMULATOR')
index=index.replace('./app.js','./'+app_name).replace('./style.css','./'+css_name)
(out/'index.html').write_text(index)
# Remove superseded outputs, including unversioned compiler intermediates.
keep={wasm_name,core_name,worker_name,backend_name,sequencer_name,sampler_name,performance_name,workspace_name,app_name,css_name,'index.html','.nojekyll','LICENSE.txt','THIRD_PARTY_NOTICES.md','THIRD_PARTY_WEB_NOTICES.txt','USER_MANUAL.md','build-info.json'}
for path in out.iterdir():
 if path.is_file() and path.name not in keep:path.unlink()
(out/'.nojekyll').write_text('')
shutil.copyfile(ROOT/'LICENSE',out/'LICENSE.txt')
shutil.copyfile(ROOT/'third_party/notices/emscripten-web.txt',out/'THIRD_PARTY_WEB_NOTICES.txt')
shutil.copyfile(ROOT/'THIRD_PARTY_NOTICES.md',out/'THIRD_PARTY_NOTICES.md')
shutil.copyfile(ROOT/'docs/USER_MANUAL.md',out/'USER_MANUAL.md')
(out/'build-info.json').write_text(json.dumps({
 'engine':'Portable C workstation, 36000 Hz','backend':'WebAssembly; no server or account',
 'source':'https://github.com/chinotto1/Fzfm',
 'commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
 'compiler':subprocess.check_output([emcc,'--version'],text=True).splitlines()[0],
 'sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.iterdir()) if p.is_file() and p.name!='build-info.json'}},indent=2)+'\n')
for path in out.iterdir():
 if path.is_file():path.chmod(0o644)
print('Static site ready:',out,flush=True)

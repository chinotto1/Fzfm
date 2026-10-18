#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real C/WASM emulator on a static server, including an iPad-sized touch session.
Requires playwright==1.55.0 and Chromium; `make web` builds the tested artifact.
The server deliberately has no /api routes. Project data stays in the browser.
"""
import functools
from http.server import SimpleHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
import shutil
import threading
from playwright.sync_api import sync_playwright
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/ui';OUT.mkdir(exist_ok=True,parents=True)
requests=[]
class Static(SimpleHTTPRequestHandler):
    def log_message(self,*_): pass
    def do_GET(self):
        requests.append(self.path)
        if not self.path.startswith('/Fzfm/'):
            self.send_error(404);return
        self.path=self.path[len('/Fzfm'):]
        super().do_GET()
server=ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Static,directory=str(ROOT/'build/site')))
threading.Thread(target=server.serve_forever,daemon=True).start()
url=f'http://127.0.0.1:{server.server_port}/Fzfm/'
try:
 with sync_playwright() as pw:
    executable=shutil.which('chromium') or pw.chromium.executable_path
    browser=pw.chromium.launch(executable_path=executable,headless=True,args=['--no-sandbox'])
    context=browser.new_context(viewport={'width':1024,'height':1366},has_touch=True,is_mobile=True,device_scale_factor=1)
    page=context.new_page();errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
    page.goto(url)
    page.wait_for_function('window.fm1 && !document.body.classList.contains("loading")',timeout=60000)
    def api(path,body=None):
        return page.evaluate('''async ([path,body])=>{
          const options=body===null?{}:{method:'POST',body:JSON.stringify(body)};
          return (await window.fm1.request(path,options)).json();
        }''',[path,body])
    def state():return api('/api/state')
    def tap(index):
        page.locator(f'#buttons [data-index="{index}"]').tap()
        page.wait_for_timeout(70)
    def rotate(name,n):
        for _ in range(abs(n)):
            page.get_by_role('button',name=name+(' increase' if n>0 else ' decrease'),exact=True).tap()
        page.wait_for_timeout(80)
    def screen(n):assert state()['screen']==n,state()
    assert state()['bpm']==120 and state()['tracks']==0,state()
    assert page.evaluate('window.fm1.backend')=='wasm'
    assert page.locator('#buttons button').count()==14
    assert page.locator('#keys button').count()==27
    assert page.get_by_role('slider').count()==7
    assert state()['persistence'],state()
    # Real touch controls create/capture a kit; OCT+ preserves the octave.
    tap(3);screen(9);tap(3);screen(10);tap(3);screen(7)
    page.locator('#test-hit').tap();page.wait_for_timeout(150)
    assert state()['regions']==1,state()
    octave=state()['octave'];tap(1);screen(0);assert state()['octave']==octave
    energy=page.evaluate('''async ()=>{
      const cmd=async value=>window.fm1.request('/api/command',{body:JSON.stringify({type:1,index:0,value})});
      await cmd(1);
      const audio=new Float32Array(await(await window.fm1.request('/api/audio')).arrayBuffer());
      await cmd(0);return Math.max(...audio.map(Math.abs));
    }''')
    assert energy>.001,energy
    # Synthesis/editor run from the same WASM model, not a JS approximation.
    tap(10);tap(3);rotate('KNOB 1',1);tap(3);rotate('KNOB 1',1);tap(3);screen(0)
    assert state()['tracks']==2
    tap(6);screen(1);rotate('SELECT',1);assert state()['scope']==1
    rotate('KNOB 1',5);tap(3);assert state()['picker']==1
    page.locator('#keys [data-key="18"]').tap();tap(8)
    assert state()['picker']==0
    for _ in range(6):tap(6)
    assert state()['tab']==6
    rotate('KNOB 1',1);rotate('KNOB 2',1);rotate('KNOB 3',12)
    assert state()['error']==0,state()
    tap(8);tap(11);screen(4)
    page.locator('#keys [data-key="0"]').tap()
    tap(12);assert state()['playing']
    tap(9);assert 'QUEUED' in state()['status'],state()
    tap(12);assert not state()['playing'] and not state()['dirty'],state()
    generation=state()['generation'];assert generation>0
    tap(8)
    page.locator('#audio').tap()
    page.wait_for_timeout(250)
    assert page.locator('#audio').inner_text()=='Sound on'
    page.locator('#keys [data-key="7"]').tap()
    page.locator('#audio').tap();assert page.locator('#audio').inner_text()=='Enable sound'
    page.screenshot(path=str(OUT/'ipad-web-emulator.png'),full_page=True)
    # IndexedDB durability: a new module instance automatically restores SAVE.
    page.reload();page.wait_for_function('window.fm1 && !document.body.classList.contains("loading")',timeout=60000)
    assert state()['tracks']==2 and state()['generation']==generation and not state()['dirty'],state()
    # Backup is a real FM1P file; malformed restore preserves the instrument.
    restored=page.evaluate('''async ()=>{
      const bytes=await(await fm1.request('/api/backup')).arrayBuffer();
      const magic=new TextDecoder().decode(bytes.slice(0,4));
      const bad=await(await fm1.request('/api/restore',{body:new TextEncoder().encode('broken').buffer})).json();
      const good=await(await fm1.request('/api/restore',{body:bytes})).json();
      return {magic,bad:bad.result,good:good.result,tracks:good.state.tracks};
    }''')
    assert restored=={'magic':'FM1P','bad':4,'good':0,'tracks':2},restored
    page.on('dialog',lambda d:d.accept())
    # Mobile layout and all relative assets also work under the repository subpath.
    for width in [390,320]:
        page.set_viewport_size({'width':width,'height':900});page.wait_for_timeout(100)
        assert page.evaluate('document.documentElement.scrollWidth<=innerWidth'),width
        page.screenshot(path=str(OUT/f'static-mobile-{width}.png'),full_page=True)
    # The demo is synthesized on-device in the worker; no shared server session.
    fresh=browser.new_context(viewport={'width':1024,'height':768},has_touch=True,is_mobile=True)
    demo_page=fresh.new_page();demo_page.on('pageerror',lambda e:errors.append(str(e)))
    demo_page.goto(url)
    demo_page.wait_for_function('window.fm1 && !document.body.classList.contains("loading")',timeout=60000)
    demo_page.locator('#demo').tap()
    demo_page.wait_for_function('window.fm1.state().tracks===2',timeout=60000)
    result=demo_page.evaluate("async ()=>(await(await fm1.request('/api/state')).json())")
    assert result['tracks']==2 and result['screen']==0,result
    result=demo_page.evaluate("async ()=>(await(await fm1.request('/api/command',{body:JSON.stringify({type:2,index:1,value:-10})})).json())")
    assert result['state']['regions']==16,result
    fresh.close()
    assert not errors,errors
    assert not any('/api/' in p for p in requests),requests
    assert any(p.endswith('.wasm') for p in requests),requests
    browser.close()
    print('Static WASM PASS: iPad-size touch capture/playback, synthesis, editing/pickers, steps, queued SAVE, IndexedDB reload, validated backups, audio unlock/mute, mobile layouts; no server API calls or JS errors.')
finally:
 server.shutdown();server.server_close()

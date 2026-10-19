#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Accessible touch creation/editing and microphone/file capture on the C/WASM site.
Uses Chromium's synthetic microphone; no physical microphone or user files.
Set FM1_TEST_URL to run the same checks against a deployed site in a fresh profile.
"""
import functools
from http.server import SimpleHTTPRequestHandler,ThreadingHTTPServer
import math
import os
from pathlib import Path
import shutil
import struct
import threading
from playwright.sync_api import sync_playwright
ROOT=Path(__file__).resolve().parents[2]
server=None
url=os.environ.get('FM1_TEST_URL')
if not url:
    class Handler(SimpleHTTPRequestHandler):
        def log_message(self,*_): pass
        def do_GET(self):
            if not self.path.startswith('/Fzfm/'):
                self.send_error(404);return
            self.path=self.path[len('/Fzfm'):]
            super().do_GET()
    server=ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(ROOT/'build/site')))
    threading.Thread(target=server.serve_forever,daemon=True).start()
    url=f'http://127.0.0.1:{server.server_port}/Fzfm/'
try:
 with sync_playwright() as pw:
    options={}
    if os.environ.get('FM1_TEST_URL') and os.environ.get('HTTPS_PROXY'):
        options['proxy']={'server':os.environ['HTTPS_PROXY']}
    browser=pw.chromium.launch(executable_path=shutil.which('chromium') or pw.chromium.executable_path,headless=True,args=['--no-sandbox','--use-fake-ui-for-media-stream','--use-fake-device-for-media-stream'],**options)
    context=browser.new_context(viewport={'width':1024,'height':1366},has_touch=True,is_mobile=True,permissions=['microphone'])
    page=context.new_page();errors=[];requests=[]
    page.on('pageerror',lambda e:errors.append(str(e)))
    page.on('request',lambda r:requests.append(r.url))
    page.goto(url,wait_until='domcontentloaded')
    page.wait_for_function('window.fm1 && !document.body.classList.contains("loading")',timeout=60000)
    page.evaluate('fm1.stopSound()')
    def state():return page.evaluate("async ()=>(await(await fm1.request('/api/state')).json())")
    def wait(condition):
        try:page.wait_for_function(condition,timeout=20000)
        except Exception:
            page.screenshot(path=str(ROOT/'build/ui/workspace-failure.png'),full_page=True)
            print('Failure:',condition,'status:',page.locator('#runtime-message').inner_text(),'JS errors:',errors,flush=True)
            raise
    def tap(id):page.locator('#'+id).tap()
    def number(id,value):
        page.locator('#'+id).fill(str(value));page.locator('#'+id).press('Tab')
    def create(kind):
        tap('new-track');page.locator(f'[data-create="{kind}"]').tap()
        page.wait_for_selector('#new-track-dialog',state='hidden')
    # Cancellation never leaves an empty track. Each choice creates its pattern.
    tap('new-track');tap('cancel-new-track');assert state()['tracks']==0
    create(5)
    wait('fm1.state().tracks===1 && fm1.state().instrumentType===2 && fm1.state().tab===6')
    assert 'synth' in page.locator('#track-choice option:checked').inner_text()
    # Physical EDIT has exactly the synth pages; the visual editor follows it.
    for tab in (3,4,5,6,3,4,5,6):
        page.locator('#buttons [data-index="6"]').tap()
        wait(f'fm1.state().tab==={tab}')
        assert page.locator('#synth-editor' if tab==6 else '#mod-editor').is_visible()
        assert not page.locator('#scope-label').is_visible()
    # Entering Sampler from a synth proposes a separate sampled instrument.
    wave=state()['editor']['wave']
    tap('sample-audio');wait('!document.querySelector("#sampling-editor").hidden')
    assert page.locator('#sampler-type').input_value()=='1'
    assert page.locator('#sampler-instrument').input_value()=='0'
    assert page.locator('.sample-slot.filled').count()==0
    assert state()['tracks']==1 and state()['instrumentType']==2 and state()['editor']['wave']==wave
    tap('open-synthesis');wait('fm1.state().tab===6')
    tap('synth-additive');page.locator('[data-recipe="0"]').tap()
    wait('fm1.state().editor.additive===true')
    e=state()['editor'];assert len(e['harmonics'])==48 and e['harmonics'][0]==256
    page.locator('#preview-note').hover();page.mouse.down()
    wait('fm1.state().voices>0')
    assert page.locator('#audio').inner_text()=='Sound on'
    page.mouse.up();wait('fm1.state().voices===0')
    tap('audio')
    number('partial-number',48);wait('fm1.state().editor.harmonic===47')
    number('partial-level',128);wait('fm1.state().editor.harmonics[47]===128')
    page.locator('#harmonic-graph').scroll_into_view_if_needed()
    box=page.locator('#harmonic-graph').bounding_box()
    page.touchscreen.tap(box['x']+box['width']*.32,box['y']+box['height']*.3)
    wait('fm1.state().editor.harmonics.filter(v=>v>0).length>=3')
    page.locator('[data-wave="1"]').tap();wait('fm1.state().editor.additive===false')
    triangle=state()['editor']['wave'];assert max(triangle)>29000 and min(triangle)<-29000
    page.locator('[data-wave="3"]').tap()
    wait('fm1.state().editor.wave[0]===30000 && fm1.state().editor.wave[700]===-30000')
    tap('synth-draw')
    before=state()['editor']['wave']
    page.locator('#wave-graph').scroll_into_view_if_needed()
    box=page.locator('#wave-graph').bounding_box()
    page.touchscreen.tap(box['x']+box['width']*.25,box['y']+box['height']*.45)
    assert not page.locator('#apply-drawing').is_disabled()
    assert state()['editor']['wave']==before,'unapplied drawing changed the sound'
    tap('apply-drawing');wait('fm1.state().editor.wave.some((v,i)=>i<512 && v!==30000)')
    # Read the exact edited location, then undo the whole applied drawing.
    applied=state()['editor']['wave'];assert applied!=before
    tap('undo-synthesis');wait('fm1.state().editor.wave[250]===30000')
    assert state()['editor']['wave']==before
    tap('open-envelope');wait('fm1.state().tab===3')
    page.wait_for_selector('#env-time')
    old_graph=page.locator('#mod-graph').screenshot()
    number('env-time',700);wait('fm1.state().editor.amp.stages[0][0]===700')
    number('env-level',.6);wait('fm1.state().editor.amp.stages[0][1]>.59 && fm1.state().editor.amp.stages[0][1]<.61')
    assert page.locator('#mod-graph').screenshot()!=old_graph
    tap('open-filter-envelope');wait('fm1.state().tab===4');page.wait_for_selector('#env-time')
    number('env-time',420);wait('fm1.state().editor.filter.stages[0][0]===420')
    assert state()['editor']['amp']['stages'][0][0]==700
    tap('open-lfo');wait('fm1.state().tab===5');page.wait_for_selector('#lfo-rate')
    number('lfo-rate',2.5);wait('fm1.state().editor.lfo.rate===2.5')
    number('lfo-depth',.75);wait('fm1.state().editor.lfo.depth===.75')
    page.locator('#lfo-shape').select_option('2');wait('fm1.state().editor.lfo.shape===2')
    samples=state()['editor']['lfo']['samples'];assert max(samples)>24000 and min(samples)<-24000
    page.locator('#lfo-shape').select_option('4');wait('fm1.state().editor.lfo.shape===4')
    samples=state()['editor']['lfo']['samples']
    assert len(set(samples))==3 and samples[0]==samples[50] and samples[90]!=samples[0],samples
    page.locator('#lfo-phase').select_option('1');wait('fm1.state().editor.lfo.free===true && fm1.state().editor.lfo.shape===4')
    page.screenshot(path=str(ROOT/'build/ui/workspace-ipad-lfo.png'),full_page=True)
    # Drum kit sampling is discoverable and uses real capture, not a JS mock.
    create(0);wait('fm1.state().tracks===2 && fm1.state().screen===7')
    tap('capture-test');wait('fm1.state().regions===1')
    tap('capture-test');wait('fm1.state().regions===2')
    # Local audio decoder/SRC -> same C capture slot, retaining the whole take.
    pcm=b''.join(struct.pack('<h',int(12000*math.sin(i*.13))) for i in range(8820))
    wav=b'RIFF'+struct.pack('<I',36+len(pcm))+b'WAVEfmt '+struct.pack('<IHHIIHH',16,1,1,44100,88200,2,16)+b'data'+struct.pack('<I',len(pcm))+pcm
    page.locator('#sample-file').set_input_files({'name':'take.wav','mimeType':'audio/wav','buffer':wav})
    wait('fm1.state().regions===3')
    tap('audio');wait('document.querySelector("#audio").textContent==="Sound on"')
    tap('record-mic');page.wait_for_function('document.querySelector("#record-mic").textContent==="Stop recording"',timeout=15000)
    assert page.locator('#audio').inner_text()=='Sound paused'
    page.wait_for_timeout(1200);tap('record-mic');wait('fm1.state().regions===4 && document.querySelector("#audio").textContent==="Sound on"')
    tap('audio');wait('document.querySelector("#audio").textContent==="Sound off"')
    tap('finish-sampling');wait('fm1.state().screen===0')
    energy=page.evaluate('''async ()=>{
      const cmd=async value=>fm1.request('/api/command',{body:JSON.stringify({type:1,index:0,value})});
      await cmd(1);const audio=new Float32Array(await(await fm1.request('/api/audio')).arrayBuffer());await cmd(0);
      return Math.max(...audio.map(Math.abs));
    }''');assert energy>.001
    # Track selector returns to the first synth with its independent parameters.
    first=state()['editor']['tracks'][0]['id'];page.locator('#track-choice').select_option(str(first))
    wait('fm1.state().instrumentType===2')
    assert state()['editor']['amp']['stages'][0][0]==700
    tap('open-synthesis');wait('fm1.state().tab===6');tap('synth-additive')
    page.locator('[data-recipe="2"]').tap();wait('fm1.state().editor.additive===true')
    levels=state()['editor']['harmonics'];assert levels[0]==256 and levels[1]==0 and levels[2]>0
    # New synth choice is visible at every phone width, with native-dialog cancel.
    for width in (390,320):
        page.set_viewport_size({'width':width,'height':900});page.wait_for_timeout(150)
        assert page.evaluate('document.documentElement.scrollWidth<=innerWidth'),width
        tap('new-track');assert page.locator('[data-create="4"]').is_visible();tap('cancel-new-track')
    page.set_viewport_size({'width':1024,'height':1366})
    create(4);wait('fm1.state().tracks===3')
    assert page.locator('#drawing-controls').is_visible()
    page.screenshot(path=str(ROOT/'build/ui/workspace-ipad-synthesis.png'),full_page=True)
    # The new editor state survives an actual local SAVE and module reload.
    page.evaluate("async ()=>fm1.request('/api/command',{body:JSON.stringify({type:5,index:0,value:0})})")
    page.reload();page.wait_for_function('window.fm1 && !document.body.classList.contains(\"loading\")',timeout=60000)
    assert state()['tracks']==3 and not state()['dirty']
    first=state()['editor']['tracks'][0]['id'];page.locator('#track-choice').select_option(str(first))
    wait('fm1.state().instrumentType===2 && fm1.state().editor.additive===true')
    assert state()['editor']['amp']['stages'][0][0]==700
    assert state()['editor']['filter']['stages'][0][0]==420
    assert state()['editor']['lfo']['shape']==4 and state()['editor']['lfo']['free']
    assert state()['editor']['harmonics'][2]>0 and state()['editor']['harmonics'][1]==0
    assert not errors,errors
    assert not any('/api/' in r for r in requests),requests
    browser.close()
    print('Workspace PASS: visible track creation/cancel/selection, 48 harmonics, preset waves, touch drawing/apply/undo, independent graphical envelopes, C LFO plots, two test hits + WAV + microphone capture, audible kit, 320/390px layouts; no API fetches or JS errors.')
finally:
 if server:server.shutdown();server.server_close()

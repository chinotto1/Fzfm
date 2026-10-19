#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Touch piano roll, lossless take quantization and a visual slot sampler.
FM1_TEST_URL optionally exercises a deployed site using a fresh browser profile.
"""
import functools
from http.server import SimpleHTTPRequestHandler,ThreadingHTTPServer
import os
from pathlib import Path
import shutil
import threading
from playwright.sync_api import sync_playwright
ROOT=Path(__file__).resolve().parents[2]
server=None;url=os.environ.get('FM1_TEST_URL')
if not url:
 class Handler(SimpleHTTPRequestHandler):
  def log_message(self,*_):pass
  def do_GET(self):
   if not self.path.startswith('/Fzfm/'):self.send_error(404);return
   self.path=self.path[len('/Fzfm'):];super().do_GET()
 server=ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(ROOT/'build/site')))
 threading.Thread(target=server.serve_forever,daemon=True).start();url=f'http://127.0.0.1:{server.server_port}/Fzfm/'
try:
 with sync_playwright() as pw:
  extra={'proxy':{'server':os.environ['HTTPS_PROXY']}} if os.environ.get('FM1_TEST_URL') and os.environ.get('HTTPS_PROXY') else {}
  browser=pw.chromium.launch(executable_path=shutil.which('chromium') or pw.chromium.executable_path,headless=True,args=['--no-sandbox'],**extra)
  context=browser.new_context(viewport={'width':1024,'height':1366},has_touch=True,is_mobile=True)
  page=context.new_page();errors=[];requests=[]
  page.on('pageerror',lambda e:errors.append(str(e)));page.on('request',lambda r:requests.append(r.url))
  page.goto(url,wait_until='domcontentloaded');page.wait_for_function('window.fm1&&!document.body.classList.contains("loading")',timeout=60000)
  page.evaluate('fm1.stopSound()')
  def state():return page.evaluate('async ()=>(await(await fm1.request("/api/state")).json())')
  def notes():return page.evaluate('async ()=>(await(await fm1.request("/api/notes",{body:JSON.stringify({from:0,to:1000000})})).json())')
  def wait(expr):
   try:page.wait_for_function(expr,timeout=20000)
   except Exception:
    page.screenshot(path=str(ROOT/'build/ui/piano-sampler-failure.png'),full_page=True)
    print('Failed:',expr,'status:',page.locator('#runtime-message').inner_text(),'errors:',errors,flush=True);raise
  def wait_notes(expr):wait('async()=>{const data=await(await fm1.request("/api/notes",{body:JSON.stringify({from:0,to:1000000})})).json();return '+expr+';}')
  def tap(id):page.locator('#'+id).tap()
  def number(id,value):page.locator('#'+id).fill(str(value));page.locator('#'+id).press('Tab')
  # Sampling is one page even when the project is empty; selector needs no SEL.
  tap('sample-audio');page.wait_for_selector('#sample-slots')
  assert not page.locator('#new-track-dialog').is_visible() and state()['tracks']==0
  assert page.locator('#sample-slots button').count()==16
  assert 'Free emulator memory' in page.locator('.sample-memory').inner_text()
  assert 'physical FM1 capacity is unverified' in page.locator('#sample-memory-help').inner_text()
  memory=state()['editor']['sampleMemory']
  assert memory['freeBytes']==memory['limitBytes']-memory['usedBytes']
  assert memory['recordingFrames']==(memory['freeBytes']-720000)//2
  assert page.locator('#sample-memory-time').inner_text()==f"{memory['recordingFrames']/36000:.1f} seconds"
  assert page.locator('#sample-memory-count').inner_text()==f"~{memory['recordingFrames']//36000} × 1s · ~{memory['recordingFrames']//360000} × 10s"
  assert '16 slots free' in page.locator('#capture-progress').inner_text()
  for width in [390,320,1024]:
   page.set_viewport_size({'width':width,'height':1366})
   assert page.evaluate('document.documentElement.scrollWidth<=innerWidth'),width
   assert page.locator('#sample-memory-free').is_visible()
   if width==320:page.locator('#sampling-editor').screenshot(path=str(ROOT/'build/ui/sampling-memory-mobile.png'))
  page.locator('#sampler-type').select_option('1');page.locator('#sampler-type').select_option('0')
  assert state()['tracks']==0,'choosing a type created unwanted empty tracks'
  page.locator('#sample-slots [data-slot="7"]').tap();tap('capture-test')
  wait('fm1.state().tracks===1&&fm1.state().editor.sampleSlots[7].id>0')
  after=state()['editor']['sampleMemory']
  assert after['freeBytes']<memory['freeBytes']
  # Staging is already allocated; it must not be deducted a second time.
  assert after['recordingFrames']==(after['freeBytes']-(720000-72000))//2
  assert '15 slots free' in page.locator('#capture-progress').inner_text()
  assert page.locator('#sample-memory-meter').evaluate('(meter)=>meter.value')<1
  colors=page.evaluate("""()=>['7','0'].map(slot=>{
   const e=document.querySelector(`#sample-slots [data-slot="${slot}"]`),s=getComputedStyle(e);return [s.backgroundColor,s.fontWeight];
  })""")
  assert colors[0][0]=='rgb(82, 203, 128)' and colors[1][0]=='rgb(41, 79, 136)',colors
  assert int(colors[0][1])>=700
  assert page.locator('#sample-audio').get_attribute('aria-pressed')=='true'
  assert state()['regions']==1 and not state()['editor']['sampleSlots'][0]['id']
  assert max(state()['editor']['sampleSlots'][7]['peaks'])>1000
  page.locator('#sample-slots [data-slot="7"]').tap()
  old=state()['editor']['sampleSlots'][7]
  tap('sample-replace');assert state()['editor']['sampleSlots'][7]['asset']==old['asset']
  tap('capture-test');wait(f'fm1.state().editor.sampleSlots[7].asset!=={old["asset"]}')
  assert state()['regions']==1 and state()['editor']['sampleSlots'][7]['id']==old['id']
  tap('sample-undo');wait(f'fm1.state().editor.sampleSlots[7].asset==={old["asset"]}')
  retained=state()['editor']['sampleMemory']
  assert retained['usedBytes']>after['usedBytes'],'undo-retained PCM disappeared from the memory budget'
  page.screenshot(path=str(ROOT/'build/ui/visual-sampler-ipad.png'),full_page=True)
  # Simple instruments use root anchors; the same page selects/creates one.
  page.locator('#sampler-type').select_option('1');wait('document.querySelector("#sampler-instrument").value==="0"')
  tap('capture-test');wait('fm1.state().tracks===2&&fm1.state().instrumentType===1&&fm1.state().regions===1')
  tap('capture-test');wait('fm1.state().regions===2')
  roots=[s['note'] for s in state()['editor']['sampleSlots'] if s['id']]
  assert roots==[60,72],roots
  tap('finish-sampling');wait('fm1.state().screen===0')
  # Touch add, move, resize, velocity, delete and undo on an actual C pattern.
  tap('open-sequencer');page.wait_for_selector('#piano-roll')
  wait('document.querySelector("#roll-count").textContent.startsWith("0 notes")')
  def point(tick,note):
   page.locator('#roll-scroll').scroll_into_view_if_needed()
   b=page.locator('#piano-roll').bounding_box()
   return b['x']+52+tick/960*128,b['y']+(127-note)*24+12
  x,y=point(300,60);page.touchscreen.tap(x,y)
  wait_notes('data.notes.length===1&&data.notes[0].tick===240')
  wait('document.querySelector("#note-start").value==="0.25"')
  x,y=point(330,60);page.mouse.move(x,y);page.mouse.down();page.mouse.move(x+64,y-48,steps=8);page.mouse.up()
  wait_notes('data.notes[0].tick===720&&data.notes[0].note===62')
  tap('roll-resize');x,y=point(810,62);page.mouse.move(x,y);page.mouse.down();page.mouse.move(x+84,y,steps=8);page.mouse.up()
  wait_notes('data.notes[0].duration===720')
  number('note-velocity',64);wait_notes('data.notes[0].velocity===64')
  tap('roll-delete');wait_notes('data.notes.length===0')
  tap('seq-undo');wait_notes('data.notes.length===1&&data.notes[0].velocity===64')
  # Raw off-grid values return unchanged when quantization is switched off.
  number('note-start',.37);wait_notes('Math.abs(data.notes[0].rawTick-355.2)<.001')
  original=notes()['notes'][0]
  page.locator('#seq-quantize').select_option('240');wait_notes('data.notes[0].tick===240&&Math.abs(data.notes[0].rawTick-355.2)<.001')
  tap('roll-note-prev');number('note-velocity',80);wait_notes('data.notes[0].velocity===80&&Math.abs(data.notes[0].rawTick-355.2)<.001')
  page.locator('#seq-quantize').select_option('0');wait_notes('Math.abs(data.notes[0].tick-355.2)<.001')
  assert notes()['notes'][0]['rawDuration']==original['rawDuration']
  page.screenshot(path=str(ROOT/'build/ui/piano-roll-ipad.png'),full_page=True)
  # Record into pattern 2 with quantization already chosen. Notes played on the
  # roll keyboard use the same local MIDI/history path as the physical keys.
  page.locator('#seq-pattern').select_option('1');wait('fm1.state().editor.pattern.slot===1')
  page.locator('#seq-quantize').select_option('240');wait('fm1.state().editor.pattern.quantize===240')
  number('seq-bpm',121);wait('fm1.state().bpm===121')
  number('seq-bpm',120);wait('fm1.state().bpm===120')
  tap('seq-record');wait('fm1.state().armed')
  page.evaluate('''async()=>{
   const p=fm1.state().editor.pattern;
   const key=async(operation,note)=>(await fm1.request('/api/sequence',{body:JSON.stringify({operation,index:0,a:0,b:0,note,velocity:100,revision:p.revision,pattern:p.id})})).json();
   const frames=async value=>(await fm1.request('/api/command',{body:JSON.stringify({type:3,index:0,value})})).json();
   await key(13,60);await frames(1234);await key(14,60);await frames(6543);
   await key(13,64);await frames(1001);await key(14,64);await frames(1111);
  }''')
  wait('fm1.state().recording');tap('seq-record');wait('!fm1.state().recording&&fm1.state().playing')
  assert state()['editor']['pattern']['quantize']==240
  assert state()['bpm']==120,'the explicit tempo was replaced by first-take inference'
  tap('seq-play');wait('!fm1.state().playing')
  recorded=notes()['notes'];assert len(recorded)==2,recorded
  assert all(abs(n['tick']/240-round(n['tick']/240))<1e-7 for n in recorded)
  assert any(abs(n['rawTick']/240-round(n['rawTick']/240))>.001 for n in recorded)
  page.locator('#seq-quantize').select_option('0');wait('fm1.state().editor.pattern.quantize===0')
  raw=notes()['notes'];assert all(abs(n['tick']-n['rawTick'])<.001 for n in raw)
  page.locator('#seq-quantize').select_option('320');wait('fm1.state().editor.pattern.quantize===320')
  tap('seq-retro');wait('fm1.state().playing')
  assert state()['editor']['pattern']['quantize']==320
  tap('seq-play');wait('!fm1.state().playing')
  assert notes()['notes'],'retrospective quantization discarded the take'
  # Existing events cannot be accidentally edited through stale note indexes.
  bad=page.evaluate('''async()=>{const p=fm1.state().editor.pattern;return(await(await fm1.request('/api/sequence',{body:JSON.stringify({operation:2,index:0,a:0,b:240,note:60,velocity:100,revision:p.revision-1,pattern:p.id})})).json()).result;}''')
  assert bad==6,bad
  # Mobile widths keep single-page controls; save/reload retains notes + grid.
  for width in [390,320]:
   page.set_viewport_size({'width':width,'height':900});page.wait_for_timeout(100)
   assert page.evaluate('document.documentElement.scrollWidth<=innerWidth'),width
  tap('quick-save');wait('!fm1.state().dirty')
  page.reload();page.wait_for_function('window.fm1&&!document.body.classList.contains("loading")',timeout=60000)
  assert state()['tracks']==2 and state()['editor']['pattern']['quantize']==320
  assert notes()['notes']
  page.set_viewport_size({'width':1024,'height':1366});tap('open-sequencer')
  wait('document.querySelector("#seq-quantize").value==="320"')
  assert not errors,errors
  assert not any('/api/' in r for r in requests),requests
  browser.close()
  print('Piano/sampler PASS: live memory/time/sample-count estimates with staging and undo accounting, single-page modes + 16 slots, selected-slot capture/atomic replacement/undo, pitched roots, touch add/move/resize/velocity/delete/undo, lossless quantization and triplets, live + retrospective takes retain grid, stale-edit rejection, mobile layouts and persisted notes; no JS errors or server API.')
finally:
 if server:server.shutdown();server.server_close()

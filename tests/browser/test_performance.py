#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Channel strips, HOME hold, audible arpeggiation and native MIDI roll.
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
    page.screenshot(path=str(ROOT/'build/ui/performance-failure.png'),full_page=True)
    print('Failed:',expr,'status:',page.locator('#runtime-message').inner_text(),'errors:',errors,flush=True);raise
  def wait_notes(expr):wait('async()=>{const data=await(await fm1.request("/api/notes",{body:JSON.stringify({from:0,to:1000000})})).json();return '+expr+';}')
  def tap(id):page.locator('#'+id).tap()
  def number(id,value):page.locator('#'+id).fill(str(value));page.locator('#'+id).press('Tab')
  def physical(index):page.locator(f'#buttons [data-index="{index}"]').tap()
  def create(kind):
   tap('new-track');page.locator(f'[data-create="{kind}"]').tap();wait('!document.querySelector("#new-track-dialog").open')
  create(2);create(3);assert state()['tracks']==2
  physical(7);wait('fm1.state().screen===2&&!document.querySelector("#mixer-editor").hidden')
  assert page.locator('.mixer-strip').count()==2
  channels=state()['editor']['mixer'];first=channels[0]['id'];second=channels[1]['id']
  def fader(track,field,value):
   page.locator(f'.mixer-strip[data-track="{track}"] input[data-field="{field}"]').evaluate('(e,value)=>{e.value=value;e.dispatchEvent(new Event("input",{bubbles:true}));e.dispatchEvent(new Event("change",{bubbles:true}));}',value)
  fader(first,0,.61);wait('fm1.state().editor.mixer[0].level===.61')
  fader(first,1,-.3);wait('fm1.state().editor.mixer[0].pan===-.3')
  fader(first,3,.25);wait('fm1.state().editor.mixer[0].sends[1]===.25')
  page.locator(f'.mixer-strip[data-track="{first}"] button[data-field="6"]').tap();wait('fm1.state().editor.mixer[0].mute')
  page.locator(f'.mixer-strip[data-track="{second}"] button[data-field="7"]').tap();wait('fm1.state().editor.mixer[1].solo')
  assert state()['editor']['selected']==second
  page.screenshot(path=str(ROOT/'build/ui/mixer-ipad.png'),full_page=True)
  # HOME hold consumes its release and opens commands; ARP opens settings.
  home=page.locator('#buttons [data-index="8"]');home.hover();page.mouse.down();page.wait_for_timeout(650);page.mouse.up()
  wait('fm1.state().screen===8');physical(10);wait('fm1.state().screen===15&&!document.querySelector("#arp-editor").hidden')
  page.locator('#arp-mode').select_option('1');wait('fm1.state().editor.arp.mode===1')
  page.locator('#arp-rate').select_option('3');number('arp-octaves',2);wait('fm1.state().editor.arp.octaves===2')
  number('arp-gate',50);number('arp-swing',25);wait('fm1.state().editor.arp.swing===.25')
  number('arp-probability',100);page.locator('#arp-latch').select_option('1');wait('fm1.state().editor.arp.latch')
  page.locator('#arp-order').select_option('1');wait('fm1.state().editor.arp.order')
  assert not state()['playing']
  # Record live MIDI through the C arp while transport is stopped. The roll
  # must contain the generated melody and gates, rather than the input chord.
  physical(13);wait('fm1.state().armed')
  energy=page.evaluate("""async()=>{
   const seq=async(operation,note)=>(await fm1.request('/api/sequence',{body:JSON.stringify({operation,index:0,a:0,b:0,note,velocity:100,revision:0,pattern:0})})).json();
   for(const n of [67,60,64])await seq(13,n);
   await fm1.request('/api/command',{body:JSON.stringify({type:3,index:0,value:27001})});
   let peak=0;for(let i=0;i<12;i++){const a=new Float32Array(await(await fm1.request('/api/audio')).arrayBuffer());for(const v of a)peak=Math.max(peak,Math.abs(v));}
   await fm1.request('/api/performance',{body:JSON.stringify({operation:2,track:fm1.state().editor.selected,field:1,value:.2})});
   for(const n of [67,60,64])await seq(14,n);
   return peak;
  }""")
  assert energy>.001,energy
  wait('fm1.state().recording');physical(13);wait('fm1.state().playing&&!fm1.state().recording')
  tap('arp-stop');wait('!fm1.state().playing&&fm1.state().screen===15')
  assert state()['bpm']==120,'recording the clocked arp changed the established tempo'
  page.screenshot(path=str(ROOT/'build/ui/arpeggiator-ipad.png'),full_page=True)
  physical(11);wait('fm1.state().screen===16&&!document.querySelector("#sequencer-editor").hidden')
  wait_notes('data.notes.length>=6')
  data=notes()['notes'];assert len({n['note'] for n in data})>=4
  assert state()['editor']['pattern']['events']>len(data)*2,'mixer motion was not recorded'
  assert all(n['duration']>0 for n in data)
  # The physical display contains the piano-note graph as well as the web roll.
  color=page.evaluate("""async()=>{const b=await createImageBitmap(await(await fm1.request('/api/screen.png')).blob());const c=document.createElement('canvas');c.width=c.height=240;const x=c.getContext('2d');x.drawImage(b,0,0);b.close();const d=x.getImageData(25,66,208,120).data;let green=0;for(let i=0;i<d.length;i+=4)if(d[i]===82&&d[i+1]===203&&d[i+2]===128)green++;return green;}""")
  assert color>5,color
  page.wait_for_timeout(180)
  page.screenshot(path=str(ROOT/'build/ui/native-piano-roll-ipad.png'),full_page=True)
  octave=state()['octave'];scroll_top=page.locator('#roll-scroll').evaluate('e=>e.scrollTop');physical(1);wait(f'fm1.state().octave==={octave+1}&&fm1.state().screen===16')
  assert page.locator('#roll-scroll').evaluate('e=>e.scrollTop')==scroll_top-288
  physical(0);wait(f'fm1.state().octave==={octave}')
  # All four sequencer screens remain reachable from the actual SEQ button.
  for screen in [4,5,6,16]:physical(11);wait(f'fm1.state().screen==={screen}')
  for width in [390,320]:
   page.set_viewport_size({'width':width,'height':900});tap('open-mixer');wait('fm1.state().screen===2')
   assert page.evaluate('document.documentElement.scrollWidth<=innerWidth'),width
   tap('open-arp');wait('fm1.state().screen===15')
   assert page.evaluate('document.documentElement.scrollWidth<=innerWidth'),width
  tap('quick-save');wait('!fm1.state().dirty');page.reload();page.wait_for_function('window.fm1&&!document.body.classList.contains("loading")',timeout=60000)
  assert state()['tracks']==2 and state()['editor']['mixer'][0]['level']==.61
  assert state()['editor']['mixer'][0]['mute'] and state()['editor']['mixer'][1]['solo']
  assert state()['editor']['arp']['mode']==1 and state()['editor']['arp']['latch'] and state()['editor']['arp']['octaves']==2
  assert notes()['notes']
  assert not errors,errors
  assert not any('/api/' in r for r in requests),requests
  browser.close();print('Performance PASS: GLO channel strips + independent faders/pan/sends/mute/solo, HOME hold commands, ARP settings + audible generated MIDI recording with PLAY stopped, native piano roll on first SEQ press, four-screen sequence cycle, mobile layouts and persisted settings/notes.')
finally:
 if server:server.shutdown();server.server_close()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real pointer/touch rotaries, default audio and explicit mute lifecycle.
FM1_TEST_URL optionally verifies the deployed emulator in a fresh profile.
"""
import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import shutil
import threading
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[2]
(ROOT/'build/ui').mkdir(parents=True, exist_ok=True)
server = None
url = os.environ.get('FM1_TEST_URL')
if not url:
    class Handler(SimpleHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_GET(self):
            if not self.path.startswith('/Fzfm/'):
                self.send_error(404)
                return
            self.path = self.path[len('/Fzfm'):]
            super().do_GET()
    server = ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(Handler, directory=str(ROOT/'build/site')))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{server.server_port}/Fzfm/'

# Some automation drivers activate the page during evaluate(). Gate only resume
# until a trusted input to exercise Safari's pending-promise path deterministically.
PROBE = '''
window.audioProbe={contexts:[],peak:0,started:0,blocked:false,gesture:false,pending:[]};
for(const type of ['pointerdown','keydown'])document.addEventListener(type,event=>{
 if(event.isTrusted)audioProbe.gesture=true;
},{capture:true});
const Original=window.AudioContext;
window.AudioContext=class extends Original {
 constructor(...args){
  super(...args);audioProbe.contexts.push(this);
  if(audioProbe.blocked&&!audioProbe.gesture)this.suspend();
 }
 resume(){
  if(audioProbe.blocked&&!audioProbe.gesture)
   return new Promise(resolve=>audioProbe.pending.push(resolve));
  return super.resume().then(()=>{for(const resolve of audioProbe.pending.splice(0))resolve();});
 }
 createBufferSource(){
  const node=super.createBufferSource(),start=node.start;
  node.start=function(...args){
   audioProbe.started++;
   if(this.buffer)for(const v of this.buffer.getChannelData(0))audioProbe.peak=Math.max(audioProbe.peak,Math.abs(v));
   return start.apply(this,args);
  };
  return node;
 }
};
'''
try:
    with sync_playwright() as pw:
        executable = shutil.which('chromium') or pw.chromium.executable_path
        extra = {'proxy': {'server': os.environ['HTTPS_PROXY']}} if os.environ.get('FM1_TEST_URL') and os.environ.get('HTTPS_PROXY') else {}
        def launch(policy):
            return pw.chromium.launch(executable_path=executable, headless=True, args=['--no-sandbox', '--autoplay-policy='+policy], **extra)
        browser = launch('document-user-activation-required')
        context = browser.new_context(viewport={'width': 1024, 'height': 1366}, has_touch=True, is_mobile=True)
        page = context.new_page()
        page.add_init_script(PROBE.replace("blocked:false", "blocked:true"))
        errors = []
        page.on('pageerror', lambda e: errors.append(str(e)))
        def wait(expr):
            try: page.wait_for_function(expr, timeout=20000)
            except Exception:
                page.screenshot(path=str(ROOT/'build/ui/rotary-audio-failure.png'), full_page=True)
                print('Failed:', expr, 'status:', page.locator('#runtime-message').inner_text(), 'errors:', errors, flush=True)
                raise
        def state(): return page.evaluate('async ()=>(await(await fm1.request("/api/state")).json())')
        def tap(index): page.locator(f'#buttons [data-index="{index}"]').tap()
        def near(field, value): wait(f'Math.abs(fm1.state().{field}-{value})<.00001')
        page.goto(url, wait_until='domcontentloaded')
        wait('window.fm1&&!document.body.classList.contains("loading")')
        # Simulate a pending autoplay resume even on headless browsers that permit it.
        # Scheduling and gesture activation still use the real AudioContext.
        assert page.locator('#audio').inner_text() == 'Sound ready', (page.locator('#audio').inner_text(), page.evaluate('audioProbe.contexts.map(c=>c.state)'), errors)
        page.locator('#new-track').tap()
        page.locator('[data-create="5"]').tap()
        wait('fm1.state().instrumentType===2 && document.querySelector("#audio").textContent==="Sound on"')
        tap(8)
        key = page.locator('#keys [data-key="7"]')
        key.hover(); page.mouse.down()
        wait('audioProbe.peak>.001 && audioProbe.contexts[0].state==="running"')
        page.mouse.up(); wait('fm1.state().voices===0')
        assert page.locator('.wheel button').count() == 0
        assert page.get_by_role('slider').count() == 7
        for dial in page.get_by_role('slider').all():
            box = dial.bounding_box()
            assert box['width'] >= 44 and box['height'] >= 44
            assert dial.evaluate('e=>getComputedStyle(e).touchAction') == 'none'
            assert dial.get_attribute('aria-valuenow') is not None
        volume = page.get_by_role('slider', name='KNOB 3', exact=True)
        def mouse_drag(dial, dy):
            dial.scroll_into_view_if_needed(); box = dial.bounding_box()
            x, y = box['x']+box['width']/2, box['y']+box['height']/2
            page.mouse.move(x, y); page.mouse.down(); page.mouse.move(x, y+dy, steps=4); page.mouse.up()
        before = volume.evaluate('e=>e.style.getPropertyValue("--dial-angle")')
        mouse_drag(volume, -32); near('volume', .88)
        assert volume.evaluate('e=>e.style.getPropertyValue("--dial-angle")') != before
        mouse_drag(volume, 16); near('volume', .84)
        assert abs(float(volume.get_attribute('aria-valuenow'))-.84) < .00001
        # Capture keeps working outside the knob; release stops further movement.
        mouse_drag(volume, -80); near('volume', 1.04)
        page.mouse.move(1, 1); page.wait_for_timeout(100); near('volume', 1.04)
        cutoff = page.get_by_role('slider', name='KNOB 1', exact=True)
        cutoff.hover(); page.mouse.wheel(0, 120); near('cutoff', .97)
        cutoff.dispatch_event('wheel', {'deltaY': 0}); page.wait_for_timeout(100); near('cutoff', .97)
        resonance = page.get_by_role('slider', name='KNOB 2', exact=True)
        resonance.press('ArrowUp'); near('resonance', .01)
        resonance.press('Shift+ArrowUp'); near('resonance', .11)
        pan = page.get_by_role('slider', name='KNOB 4', exact=True)
        pan.press('PageUp'); near('pan', .1)
        pan.press('ArrowLeft'); near('pan', .09)
        # Real touch events must turn the knob without scrolling the document.
        volume.scroll_into_view_if_needed(); box = volume.bounding_box()
        x, y = box['x']+box['width']/2, box['y']+box['height']/2
        scroll = page.evaluate('scrollY')
        cdp = context.new_cdp_session(page)
        cdp.send('Input.dispatchTouchEvent', {'type': 'touchStart', 'touchPoints': [{'x': x, 'y': y}]})
        for dy in (8, 16, 24, 32, 40):
            cdp.send('Input.dispatchTouchEvent', {'type': 'touchMove', 'touchPoints': [{'x': x, 'y': y-dy}]})
        cdp.send('Input.dispatchTouchEvent', {'type': 'touchEnd', 'touchPoints': []})
        near('volume', 1.14)
        assert abs(page.evaluate('scrollY')-scroll) < 1
        cdp.send('Input.dispatchTouchEvent', {'type': 'touchStart', 'touchPoints': [{'x': x, 'y': y}]})
        cdp.send('Input.dispatchTouchEvent', {'type': 'touchCancel', 'touchPoints': []})
        assert page.locator('.dial.dragging').count() == 0
        # Playing keyboard notes still works while a rotary has keyboard focus.
        volume.focus(); page.evaluate('audioProbe.peak=0'); page.keyboard.down('z')
        wait('audioProbe.peak>.001'); page.keyboard.up('z'); wait('fm1.state().voices===0')
        # Selection encoders reach the C model as well as their visual indicators.
        first = state()['editor']['selected']
        page.locator('#new-track').tap(); page.locator('[data-create="5"]').tap(); tap(8)
        second = state()['editor']['selected']; assert first != second
        page.get_by_role('slider', name='ALGORITHM', exact=True).press('ArrowDown')
        wait(f'fm1.state().editor.selected==={first}'); near('volume', 1.14)
        tap(4)
        page.get_by_role('slider', name='SELECT', exact=True).press('ArrowUp')
        wait('fm1.state().scope===1')
        tap(8)
        # Browser interruption pauses safely, then recovers without an extra sound toggle.
        page.evaluate('''()=>{Object.defineProperty(document,'hidden',{configurable:true,value:true});document.dispatchEvent(new Event('visibilitychange'));}''')
        wait('audioProbe.contexts[0].state==="suspended"')
        page.evaluate('''()=>{Object.defineProperty(document,'hidden',{configurable:true,value:false});document.dispatchEvent(new Event('visibilitychange'));}''')
        wait('document.querySelector("#audio").textContent==="Sound on"')
        page.locator('#audio').tap(); wait('document.querySelector("#audio").textContent==="Sound off"')
        key.hover(); page.mouse.down(); page.mouse.up(); page.wait_for_timeout(150)
        assert page.locator('#audio').inner_text() == 'Sound off'
        assert page.evaluate('audioProbe.contexts[0].state') == 'suspended'
        page.evaluate("()=>{Object.defineProperty(document,'hidden',{configurable:true,value:true});document.dispatchEvent(new Event('visibilitychange'));}")
        page.evaluate("()=>{Object.defineProperty(document,'hidden',{configurable:true,value:false});document.dispatchEvent(new Event('visibilitychange'));}")
        page.wait_for_timeout(100)
        assert page.locator('#audio').inner_text() == 'Sound off'
        for width in (390, 320):
            page.set_viewport_size({'width': width, 'height': 900})
            assert page.evaluate('document.documentElement.scrollWidth<=innerWidth')
            assert page.locator('.wheel button').count() == 0
            for dial in page.get_by_role('slider').all(): assert dial.bounding_box()['width'] >= 44
        page.screenshot(path=str(ROOT/'build/ui/rotaries-mobile.png'), full_page=True)
        # A keyboard note can be the very first gesture; no audio-button click is needed.
        fresh = browser.new_context(viewport={'width': 1024, 'height': 1366})
        keyboard_page = fresh.new_page(); keyboard_page.add_init_script(PROBE.replace("blocked:false", "blocked:true"))
        keyboard_page.on('pageerror', lambda e: errors.append(str(e)))
        keyboard_page.goto(url, wait_until='domcontentloaded')
        keyboard_page.wait_for_function('window.fm1&&!document.body.classList.contains("loading")', timeout=60000)
        keyboard_page.evaluate('''async()=>{await fm1.request('/api/action',{body:JSON.stringify({action:0,value:5})});}''')
        keyboard_page.wait_for_function('fm1.state().instrumentType===2')
        keyboard_page.keyboard.down('z')
        keyboard_page.wait_for_function('audioProbe.peak>.001&&audioProbe.contexts[0].state==="running"')
        keyboard_page.keyboard.up('z')
        fresh.close(); browser.close()
        # Where autoplay is permitted, sound starts before any interaction at all.
        autoplay = launch('no-user-gesture-required')
        auto_page = autoplay.new_page(); auto_page.add_init_script(PROBE)
        auto_page.on('pageerror', lambda e: errors.append(str(e)))
        auto_page.goto(url, wait_until='domcontentloaded')
        auto_page.wait_for_function('window.fm1&&audioProbe.contexts[0]?.state==="running"&&document.querySelector("#audio").textContent==="Sound on"', timeout=60000)
        autoplay.close()
        assert not errors, errors
        print('Rotary/audio PASS: seven standalone accessible rotaries, mouse/touch drag and capture, wheel/keyboard/shift, live C values and track/scope selection, no scrolling or +/- buttons, 320/390px layouts, audible first tap/keyboard note, permitted autoplay, interruption recovery and persistent explicit mute.')
finally:
    if server: server.shutdown(); server.server_close()

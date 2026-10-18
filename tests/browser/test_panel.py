#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""End-to-end physical panel workflows. Requires playwright==1.55.0 and Chromium.
Run after `make build/libfm1_sim.so`: python3 tests/browser/test_panel.py.
Creates an isolated local server/project; never touches a device or user save.
"""
import array
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
from urllib.request import urlopen
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[2]
ARTIFACTS=ROOT/'build/ui'
ARTIFACTS.mkdir(parents=True,exist_ok=True)
with socket.socket() as sock:
    sock.bind(('127.0.0.1',0)); port=sock.getsockname()[1]
base=f'http://127.0.0.1:{port}'
with tempfile.TemporaryDirectory(prefix='fm1-browser-') as data:
    server=subprocess.Popen([sys.executable,str(ROOT/'host/simulator/serve.py'),'--port',str(port),'--data',data],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    try:
        for _ in range(100):
            try:
                urlopen(base+'/api/state',timeout=.2).close(); break
            except OSError: time.sleep(.05)
        else: raise AssertionError('simulator did not start')
        with sync_playwright() as pw:
            browser=pw.chromium.launch(executable_path='/usr/bin/chromium',headless=True,args=['--no-sandbox'])
            page=browser.new_page(viewport={'width':1360,'height':1100})
            errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
            page.goto(base);page.wait_for_selector('.keyboard button')
            def state(): return page.request.get(base+'/api/state').json()
            def tap(index):
                page.locator(f'#buttons [data-index="{index}"]').click()
                page.wait_for_timeout(70)
            def rotate(name,delta):
                # Dragging and keyboard actions use the real relative encoder UI.
                dial=page.get_by_role('slider',name=name,exact=True)
                dial.focus()
                for _ in range(abs(delta)): dial.press('ArrowUp' if delta>0 else 'ArrowDown')
                page.wait_for_timeout(80)
            def screen(n): assert state()['screen']==n,state()
            assert page.locator('#buttons button').count()==14
            assert page.locator('.keyboard button').count()==27
            assert page.get_by_role('slider').count()==7
            # Capture -> immediate OCT+ performance, using fast clicks.
            tap(3);screen(9);tap(3);screen(10);tap(3);screen(7)
            page.locator('#test-hit').click();page.wait_for_timeout(150)
            assert state()['regions']==1,state()
            octave=state()['octave'];tap(1);screen(0)
            assert state()['octave']==octave
            page.locator('[data-key="0"]').hover();page.mouse.down()
            page.wait_for_timeout(10)
            samples=array.array('f',page.request.get(base+'/api/audio').body())
            assert max(abs(x) for x in samples)>.001,'captured note produced silence'
            page.mouse.up()
            # Create SAMPLE/additive through actions and source menus.
            tap(10);screen(8);tap(3);screen(9)
            rotate('KNOB 1',1);tap(3);screen(10)
            rotate('KNOB 1',1);tap(3);screen(0)
            assert state()['tracks']==2 and state()['regions']==1
            tap(6);screen(1)
            rotate('SELECT',1);assert state()['scope']==1
            rotate('KNOB 1',5);tap(3);assert state()['picker']==1
            before=page.request.get(base+'/api/backup').body()
            page.locator('[data-key="18"]').click();tap(8)
            assert state()['picker']==0 and page.request.get(base+'/api/backup').body()==before
            # Naming hold fires once and does not cycle the edit tab on release.
            button=page.locator('#buttons [data-index="6"]')
            button.hover();page.mouse.down();page.wait_for_timeout(650)
            assert state()['picker']==6,state()
            page.mouse.up();page.locator('[data-key="4"]').click();page.wait_for_timeout(80)
            assert state()['instrument'].startswith('PAD '),state()
            assert state()['tab']==0
            # All nine tabs retain ONE scope; additive changes publish real PCM.
            for _ in range(6):tap(6)
            assert state()['tab']==6 and state()['scope']==1
            rotate('KNOB 1',1);rotate('KNOB 2',1);rotate('KNOB 3',32)
            assert state()['assets']>=3
            page.screenshot(path=str(ARTIFACTS/'desktop-edit.png'),full_page=True)
            tap(8);tap(11);screen(4)
            page.locator('[data-key="0"]').click();page.locator('[data-key="7"]').click()
            tap(12);assert state()['playing']==1
            tap(9);assert 'QUEUED' in state()['status'],state()
            tap(12);assert state()['playing']==0 and state()['dirty']==0,state()
            tap(11);screen(5);page.locator('[data-key="2"]').click()
            tap(11);screen(6);tap(3);assert state()['sections']==1
            tap(8);tap(2);screen(3);rotate('KNOB 1',6);rotate('KNOB 2',1)
            tap(7);screen(2);tap(8);screen(0)
            # Browser sound presentation uses the native renderer.
            page.locator('#audio').click();assert page.locator('#audio').inner_text()=='Sound on'
            page.locator('[data-key="16"]').click();page.wait_for_timeout(100)
            page.locator('#audio').click();assert not errors,errors
            backup=page.request.get(base+'/api/backup').body()
            page.on('dialog',lambda d:d.accept())
            page.locator('#restore').set_input_files({'name':'project.fm1p','mimeType':'application/octet-stream','buffer':backup})
            page.wait_for_timeout(180);assert state()['tracks']==2 and state()['sections']==1,state()
            bad=page.request.post(base+'/api/restore',data=b'bad project').json()
            assert bad['result']!=0 and bad['state']['tracks']==2
            invalid=page.request.post(base+'/api/command',data={'type':'x','index':0,'value':1})
            assert invalid.status==400
            # Mobile and narrow layouts retain all controls without page overflow.
            for width in [390,320]:
                page.set_viewport_size({'width':width,'height':900});page.wait_for_timeout(150)
                assert page.evaluate('document.documentElement.scrollWidth <= innerWidth'),f'overflow at {width}px'
                tap(6);screen(1)
                page.screenshot(path=str(ARTIFACTS/f'mobile-{width}.png'),full_page=True)
                tap(8)
            assert not errors,errors
            browser.close()
            print('Browser PASS: capture, fast clicks, playback, creation, numeric cancel, hold naming, tabs/scope, synthesis, steps, queued save, patterns/song, FX/mixer, backup/restore, sound, 390/320px layouts; no JS errors.')
    finally:
        server.terminate()
        try:server.wait(timeout=5)
        except subprocess.TimeoutExpired:server.kill();server.wait()

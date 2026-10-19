#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Hardware installer over a simulated Web MIDI device. Never accesses USB.
FM1_TEST_URL can point to the deployed /install/ page for the same smoke test.
"""
import copy
import functools
import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import threading
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/ui'
OUT.mkdir(parents=True, exist_ok=True)
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
    server = ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(Handler, directory=str(ROOT / 'build/site')))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{server.server_port}/Fzfm/install/'

def fixture(product):
    raw = bytearray((i * 37 + 13) & 255 for i in range(1200))
    for i in range(20): raw[i * 48 + 47] = ord(product[i]) + i + 1 if i < len(product) else 125
    return bytes(raw)

catalog = json.loads((ROOT / 'web/installer/releases.json').read_text())
files = {}
for release in catalog['recovery']:
    raw = fixture(release['product'])
    files[release['id']] = raw
    release.update(size=len(raw), sha256=hashlib.sha256(raw).hexdigest())
mock = (ROOT / 'tests/installer/fake-fm1.mjs').read_text().replace('export ', '')
mock += """
const raw = fixturePackage(), image = new Uint8Array(raw.length - 20);
for (let i=0;i<20;i++) image.set(raw.subarray(i*48,i*48+47),i*47);
image.set(raw.subarray(960),940);
window.__fm1Device = new FakeFM1(image);
window.__midiRequests = 0;
Object.defineProperty(navigator,'requestMIDIAccess',{value:async options=>{
  window.__midiRequests++;
  if(!options.sysex) throw new Error('SysEx permission not requested');
  return window.__fm1Device.access;
},configurable:true});
"""

try:
    with sync_playwright() as pw:
        extra = {'proxy': {'server': os.environ['HTTPS_PROXY']}} if os.environ.get('FM1_TEST_URL') and os.environ.get('HTTPS_PROXY') else {}
        browser = pw.chromium.launch(executable_path=shutil.which('chromium') or pw.chromium.executable_path, headless=True, args=['--no-sandbox'], **extra)
        context = browser.new_context(viewport={'width':1040,'height':1000}, has_touch=True)
        context.add_init_script(mock)
        page = context.new_page()
        errors = []
        page.on('pageerror', lambda error: errors.append(str(error)))
        page.route('**/releases.json', lambda route: route.fulfill(json=catalog))
        page.goto(url, wait_until='domcontentloaded')
        page.wait_for_function('document.querySelector("#current-badge").textContent === "Hardware release pending"')
        def wait(expression):
            try: page.wait_for_function(expression, timeout=30000)
            except Exception:
                page.screenshot(path=str(OUT / 'installer-failure.png'), full_page=True)
                print(page.locator('#log').inner_text(), flush=True)
                raise
        def upload(release_id, raw=None):
            r = next(r for r in catalog['recovery'] if r['id'] == release_id)
            page.locator('#firmware-file').set_input_files({'name':r['filename'], 'mimeType':'application/octet-stream', 'buffer':raw if raw is not None else files[release_id]})
        def connects():
            page.locator('#connect').click()
            wait('document.querySelector("#device-status").textContent.startsWith("Connected:")')
        assert page.evaluate('__midiRequests') == 0
        assert page.locator('#install').is_disabled()
        assert not page.locator('#package-panel').is_visible()
        assert 'working USB updater' in page.locator('#current-reason').inner_text()
        connects()
        assert page.evaluate('__fm1Device.upgrades') == 0
        page.locator('#mode-recover').click()
        assert page.locator('#firmware option').count() == 3
        bad = bytearray(files['skiff-1.3.2']); bad[1100] ^= 1
        upload('skiff-1.3.2', bytes(bad))
        wait('document.querySelector("#package-status").textContent.includes("Checksum mismatch")')
        assert page.locator('#install').is_disabled()
        upload('skiff-1.3.2')
        wait('document.querySelector("#package-status").classList.contains("valid")')
        assert page.locator('#install').is_disabled(), 'backup requirement was bypassed'
        page.locator('#backup-saved').check()
        assert page.locator('#install').is_enabled()
        page.locator('#install').click()
        assert page.locator('#confirm-dialog').is_visible()
        assert catalog['recovery'][0]['sha256'] in page.locator('#confirm-hash').inner_text()
        page.locator('#confirm-cancel').click()
        assert page.evaluate('__fm1Device.upgrades') == 0
        for width in [1040, 390, 320]:
            page.set_viewport_size({'width':width, 'height':1000})
            page.locator('#package-details').evaluate('(element)=>element.open=true')
            assert page.evaluate('document.documentElement.scrollWidth <= innerWidth'), width
            page.screenshot(path=str(OUT / f'installer-{width}.png'), full_page=True)
        page.set_viewport_size({'width':1040,'height':1000})
        page.locator('#install').click()
        page.locator('#confirm-install').click()
        wait('document.querySelector("#progress").value===100')
        assert page.evaluate('__fm1Device.bad') == 0
        assert page.evaluate('__fm1Device.dropped') == 0
        assert page.evaluate('__fm1Device.upgrades') == 2
        assert 'expected firmware identity' in page.locator('#transfer-stage').inner_text()
        # An unplug during the write must stay incomplete and allow matching resume.
        page.evaluate('__fm1Device.options.unplugPhase="write"')
        connects()
        page.locator('#install').click(); page.locator('#confirm-install').click()
        wait('document.querySelector("#transfer-stage").classList.contains("error")')
        assert page.locator('#progress').evaluate('(e)=>e.value') < 100
        assert 'recovery' in page.locator('#transfer-stage').inner_text()
        page.evaluate('delete __fm1Device.options.unplugPhase; __fm1Device.boot("ota-FM-1_713")')
        connects()
        assert page.locator('#install').inner_text() == 'Resume matching update'
        page.locator('#firmware').select_option('stock-v15')
        upload('stock-v15')
        wait('document.querySelector("#package-status").classList.contains("valid")')
        assert page.locator('#install').is_disabled()
        assert 'Skiff 1.3.2' in page.locator('#action-help').inner_text()
        count = page.evaluate('__fm1Device.upgrades')
        page.locator('#install').dispatch_event('click')
        assert page.evaluate('__fm1Device.upgrades') == count
        page.locator('#firmware').select_option('skiff-1.3.2')
        upload('skiff-1.3.2')
        wait('document.querySelector("#package-status").classList.contains("valid")')
        page.locator('#install').click(); page.locator('#confirm-install').click()
        wait('document.querySelector("#progress").value===100')
        assert page.evaluate('__fm1Device.bad') == 0
        assert page.evaluate('__fm1Device.upgrades') == count + 1
        # A device unplugged while the confirmation is open cannot be flashed.
        connects(); page.locator('#install').click()
        page.evaluate('__fm1Device.disconnect()')
        page.locator('#confirm-install').click()
        assert not page.locator('#confirm-dialog').is_visible()
        assert page.evaluate('__fm1Device.upgrades') == count + 1
        page.locator('#mode-install').click()
        page.locator('#install').dispatch_event('click')
        assert page.locator('#install').is_disabled()
        assert not page.locator('#confirm-dialog').is_visible()
        assert not errors, errors
        # Unsupported browsers can read the page but never start a device write.
        unsupported = browser.new_context(viewport={'width':390,'height':900}, has_touch=True)
        unsupported.add_init_script("Object.defineProperty(navigator,'requestMIDIAccess',{value:undefined,configurable:true})")
        mobile = unsupported.new_page()
        mobile.route('**/releases.json', lambda route: route.fulfill(json=catalog))
        mobile.goto(url)
        mobile.wait_for_selector('#browser-note')
        assert 'iPad emulator' in mobile.locator('#browser-note').inner_text()
        assert mobile.locator('#connect').is_disabled()
        assert mobile.locator('#install').is_disabled()
        mobile.screenshot(path=str(OUT / 'installer-unsupported.png'), full_page=True)
        unsupported.close()
        denied = browser.new_context()
        denied.add_init_script("Object.defineProperty(navigator,'requestMIDIAccess',{value:async()=>{throw new DOMException('denied','NotAllowedError')},configurable:true})")
        blocked = denied.new_page()
        blocked.route('**/releases.json', lambda route: route.fulfill(json=catalog))
        blocked.goto(url)
        blocked.wait_for_function('!document.querySelector("#connect").disabled')
        blocked.locator('#connect').click()
        blocked.wait_for_function('document.querySelector("#device-status").textContent.includes("permission was denied")')
        assert blocked.locator('#install').is_disabled()
        assert blocked.locator('#connect').is_enabled(), 'permission failure prevented retry'
        denied.close()
        # Broken catalog data blocks connection and every update path.
        invalid = browser.new_context()
        invalid.add_init_script(mock)
        broken = copy.deepcopy(catalog); broken['recovery'][0]['sha256'] = 'invalid'
        tab = invalid.new_page()
        tab.route('**/releases.json', lambda route: route.fulfill(json=broken))
        tab.goto(url)
        tab.wait_for_function('document.querySelector("#current-badge").textContent==="Release status unavailable"')
        assert tab.locator('#connect').is_disabled()
        assert tab.locator('#install').is_disabled()
        assert tab.evaluate('__midiRequests') == 0
        invalid.close()
        browser.close()
        print('Installer browser PASS: no automatic MIDI access, gated current build, catalog/file/hash validation, backup and explicit confirmation, byte-level simulated install, unplug/failure and matching resume, wrong-loader rejection, disconnect during review, unsupported browsers and 320/390px layouts. No physical device accessed.')
finally:
    if server: server.shutdown(); server.server_close()

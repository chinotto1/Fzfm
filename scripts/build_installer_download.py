#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a self-contained browser installer page; no firmware, server or USB access."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=Path, default=ROOT / 'build/FM1-FZ-Installer.html')
args = parser.parse_args()
source = ROOT / 'web/installer'
catalog = json.loads((source / 'releases.json').read_text())
catalog['current']['commit'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
app = (source / 'app.js').read_text()
catalog_fetch = """    const response = await fetch('./releases.json', {cache:'no-cache'});
    if (!response.ok) throw new Error('The firmware catalog could not be loaded. Reload this page.');
    const data = await response.json();"""
if app.count(catalog_fetch) != 1:
    raise SystemExit('Installer catalog loader changed; update and test the offline builder.')
app = app.replace(catalog_fetch, "    const data = JSON.parse(document.getElementById('release-data').textContent);")
modules = [source / 'vendor/fm1pkg.js', source / 'vendor/fm1ota.js', source / 'packages.js']
code = '\n'.join(p.read_text() for p in modules) + '\n' + app
code = '\n'.join(line for line in code.splitlines() if not line.startswith('import ')).replace('export ', '')
if '</script' in code.lower():
    raise SystemExit('Unexpected HTML script terminator in installer source.')
page = (source / 'index.html').read_text()
page = page.replace('<link rel="stylesheet" href="./style.css">', '<style>' + (source / 'style.css').read_text() + '</style>')
payload = json.dumps(catalog, ensure_ascii=True).replace('<', '\\u003c').replace('&', '\\u0026')
page = page.replace('<script type="module" src="./app.js"></script>', '<script id="release-data" type="application/json">' + payload + '</script><script type="module">\n' + code + '\n</script>')
page = page.replace('href="../"', 'href="https://chinotto1.github.io/Fzfm/"')
page = page.replace('href="../THIRD_PARTY_NOTICES.md"', 'href="#licences"')
page = page.replace('<a href="./FM1-FZ-Installer.html" download>Download this installer page</a>', '')
licences = '<details id="licences" class="technical"><summary>Source licences and credits</summary><pre>' + html.escape((source / 'vendor/NOTICE.md').read_text() + '\n' + (ROOT / 'LICENSE').read_text()) + '</pre></details>'
page = page.replace('</main>', licences + '</main>')
args.out.parent.mkdir(parents=True, exist_ok=True)
args.out.write_text(page)
args.out.chmod(0o644)
print(f'{args.out}: {args.out.stat().st_size} bytes, SHA-256 {hashlib.sha256(args.out.read_bytes()).hexdigest()}')

#!/usr/bin/env python3
"""Local panel simulator backed by the native C application. No external service."""
import argparse
import ctypes as C
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[2]
MAX_BODY = 32 * 1024 * 1024


def native():
    lib = C.CDLL(str(ROOT / 'build/libfm1_sim.so'))
    lib.fm_sim_create.argtypes = [C.c_char_p]
    lib.fm_sim_create.restype = C.c_void_p
    for name in ['fm_sim_destroy', 'fm_sim_pixels']:
        getattr(lib, name).argtypes = [C.c_void_p]
    lib.fm_sim_pixels.restype = C.POINTER(C.c_ubyte)
    lib.fm_sim_text.argtypes = [C.c_void_p, C.c_uint]
    lib.fm_sim_text.restype = C.c_char_p
    lib.fm_sim_metric.argtypes = [C.c_void_p, C.c_uint]
    lib.fm_sim_metric.restype = C.c_double
    lib.fm_sim_command.argtypes = [C.c_void_p, C.c_uint, C.c_uint, C.c_int]
    lib.fm_sim_render.argtypes = [C.c_void_p, C.POINTER(C.c_float), C.c_uint]
    lib.fm_sim_import_wave.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t, C.c_int]
    lib.fm_sim_restore.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t]
    lib.fm_sim_backup.argtypes = [C.c_void_p, C.POINTER(C.c_void_p), C.POINTER(C.c_size_t)]
    lib.fm_sim_free.argtypes = [C.c_void_p]
    lib.fm_sim_demo.argtypes = [C.c_void_p]
    lib.fm_sim_editor.argtypes = [C.c_void_p]
    lib.fm_sim_editor.restype = C.c_char_p
    lib.fm_sim_action.argtypes = [C.c_void_p, C.c_uint, C.c_uint]
    lib.fm_sim_parameter.argtypes = [C.c_void_p, C.c_uint, C.c_uint, C.c_uint, C.c_double]
    for name in ['draw', 'input', 'harmonics']:
        getattr(lib, 'fm_sim_'+name).argtypes = [C.c_void_p, C.c_void_p, C.c_uint]
    return lib


def png(pixels):
    def chunk(name, data):
        return struct.pack('!I', len(data)) + name + data + struct.pack('!I', zlib.crc32(name + data))
    rows = b''.join(b'\0' + bytes(245 if x else 12 for x in pixels[y*240:(y+1)*240]) for y in range(240))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!2I5B', 240, 240, 8, 0, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--data', type=Path, default=ROOT / 'build/interactive')
    args = parser.parse_args()
    args.data.mkdir(parents=True, exist_ok=True)
    lib = native()
    handle = lib.fm_sim_create(str(args.data).encode())
    if not handle:
        raise SystemExit('Native simulator could not allocate its declared host budgets')
    metric_names = ['frames','tracks','assets','bpm','playing','dirty','picker','screen','tab','scope','cutoff','resonance','volume','pan','regions','captureState','peak','captureSlot','error','sections','pattern','voices','recording','armed','candidates','historySeconds','arranging','audioBytes','fxBytes','rejectedEvents','busyFrames','instrumentType','baseNote','octave']

    def state():
        data = {name: lib.fm_sim_metric(handle, i) for i, name in enumerate(metric_names)}
        data.update(title=lib.fm_sim_text(handle, 0).decode(), status=lib.fm_sim_text(handle, 1).decode(), instrument=lib.fm_sim_text(handle, 2).decode())
        data['editor'] = json.loads(lib.fm_sim_editor(handle))
        return data

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def send(self, content, mime='application/json', code=200):
            if not isinstance(content, bytes):
                content = json.dumps(content).encode()
            self.send_response(code)
            self.send_header('Content-Type', mime)
            self.send_header('Content-Length', str(len(content)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.end_headers()
            self.wfile.write(content)

        def do_GET(self):
            route = self.path.split('?', 1)[0]
            if route.startswith('/api/') and (self.headers.get('Sec-Fetch-Site') == 'cross-site' or self.headers.get('Host', '') not in {f'127.0.0.1:{args.port}', f'localhost:{args.port}'}):
                return self.send({'error': 'invalid origin or host'}, code=403)
            if route == '/api/state':
                return self.send(state())
            if route == '/api/screen.png':
                return self.send(png(C.string_at(lib.fm_sim_pixels(handle), 240*240)), 'image/png')
            if route == '/api/audio':
                samples = (C.c_float * (3600 * 2))()
                result = lib.fm_sim_render(handle, samples, 3600)
                return self.send(bytes(samples), 'application/octet-stream', 200 if result == 0 else 500)
            if route == '/api/backup':
                data, length = C.c_void_p(), C.c_size_t()
                result = lib.fm_sim_backup(handle, C.byref(data), C.byref(length))
                if result:
                    return self.send({'error': result}, code=409)
                try:
                    return self.send(C.string_at(data, length.value), 'application/octet-stream')
                finally:
                    lib.fm_sim_free(data)
            paths = {'/': 'index.html', '/app.js': 'app.js', '/backend.js': 'backend.js', '/workspace.js': 'workspace.js', '/style.css': 'style.css'}
            if route not in paths:
                return self.send({'error': 'not found'}, code=404)
            name = paths[route]
            mime = 'text/html' if name.endswith('html') else 'text/css' if name.endswith('css') else 'text/javascript'
            return self.send((ROOT / 'web/simulator' / name).read_bytes(), mime + '; charset=utf-8')

        def do_POST(self):
            # Loopback service: reject cross-origin mutations and unexpected Hosts.
            host = self.headers.get('Host', '')
            if host not in {f'127.0.0.1:{args.port}', f'localhost:{args.port}'}:
                return self.send({'error': 'invalid host'}, code=403)
            origin = self.headers.get('Origin')
            if origin and origin not in {f'http://127.0.0.1:{args.port}', f'http://localhost:{args.port}'}:
                return self.send({'error': 'invalid origin'}, code=403)
            try:
                n = int(self.headers.get('Content-Length', '0'))
                if not 0 <= n <= MAX_BODY:
                    return self.send({'error': 'file too large'}, code=413)
                body = self.rfile.read(n)
                route = self.path.split('?', 1)[0]
                if route in {'/api/wave','/api/cycle','/api/restore'}:
                    buf = C.create_string_buffer(body)
                    result = lib.fm_sim_restore(handle, buf, len(body)) if route.endswith('restore') else lib.fm_sim_import_wave(handle, buf, len(body), route.endswith('cycle'))
                elif route in {'/api/draw', '/api/input', '/api/harmonics'}:
                    if len(body) % 2:
                        raise ValueError('PCM16 data required')
                    buf = C.create_string_buffer(body)
                    result = getattr(lib, 'fm_sim_'+route[5:])(handle, buf, len(body)//2)
                elif route in {'/api/action', '/api/parameter'}:
                    data = json.loads(body)
                    fields = ['action','value'] if route.endswith('action') else ['tab','row','field']
                    if not isinstance(data,dict) or any(type(data.get(k)) is not int or not 0 <= data[k] <= 1000000 for k in fields):
                        raise ValueError('nonnegative integer fields required')
                    if route.endswith('action'):
                        result = lib.fm_sim_action(handle,data['action'],data['value'])
                    else:
                        value = data.get('value')
                        if type(value) not in {int,float} or not -1000000 <= value <= 1000000:
                            raise ValueError('finite value required')
                        result = lib.fm_sim_parameter(handle,data['tab'],data['row'],data['field'],value)
                elif route == '/api/demo':
                    result = lib.fm_sim_demo(handle)
                elif route == '/api/command':
                    data = json.loads(body)
                    if not isinstance(data, dict) or any(type(data.get(k)) is not int for k in ['type','index','value']):
                        raise ValueError('integer command fields required')
                    if not (0 <= data['type'] <= 7 and 0 <= data['index'] <= 127 and -10000 <= data['value'] <= 36000):
                        raise ValueError('command outside bounds')
                    result = lib.fm_sim_command(handle, data['type'], data['index'], data['value'])
                else:
                    return self.send({'error': 'not found'}, code=404)
                return self.send({'result': result, 'state': state()})
            except (ValueError, TypeError, KeyError, UnicodeError):
                return self.send({'error': 'malformed request'}, code=400)

    server = HTTPServer(('127.0.0.1', args.port), Handler)
    print(f'FM1 simulator listening on 127.0.0.1:{args.port}; Ctrl-C to stop', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        lib.fm_sim_destroy(handle)


if __name__ == '__main__':
    main()

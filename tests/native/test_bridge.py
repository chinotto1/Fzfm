#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Native simulator ownership/import regression; stdlib only, no browser/server."""
import ctypes as C
import importlib.util
import math
from pathlib import Path
import struct
import tempfile
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('sim_server',ROOT/'host/simulator/serve.py')
server=importlib.util.module_from_spec(spec);spec.loader.exec_module(server)
lib=server.native()
with tempfile.TemporaryDirectory(prefix='fm1-native-') as directory:
    h=lib.fm_sim_create(directory.encode());assert h
    try:
        def command(t,i,v): return lib.fm_sim_command(h,t,i,v)
        def backup():
            p=C.c_void_p();n=C.c_size_t()
            assert lib.fm_sim_backup(h,C.byref(p),C.byref(n))==0
            try:return C.string_at(p,n.value)
            finally:lib.fm_sim_free(p)
        def metric(i):return lib.fm_sim_metric(h,i)
        def imported(payload,cycle=False):
            b=C.create_string_buffer(payload)
            return lib.fm_sim_import_wave(h,b,len(payload),cycle)
        # Valid PCM16 ordinary WAV import publishes one complete track/region.
        pcm=b''.join(struct.pack('<h',int(12000*math.sin(i*.13))) for i in range(3600))
        header=b'RIFF'+struct.pack('<I',36+len(pcm))+b'WAVEfmt '+struct.pack('<IHHIIHH',16,1,1,36000,72000,2,16)+b'data'+struct.pack('<I',len(pcm))
        assert imported(header+pcm)==0
        assert metric(1)==1 and metric(14)==1
        original=backup()
        assert imported(b'not a WAV')!=0 and backup()==original
        assert imported((header+pcm)[:-1])!=0 and backup()==original
        # Same-frame physical clicks and key release cannot stick in the bridge.
        assert command(0,6,1)==0
        command(0,6,0)
        assert metric(7)==1
        command(1,7,1);command(1,7,0)
        audio=(C.c_float*8192)();assert lib.fm_sim_render(h,audio,4096)==0
        assert metric(21)==0
        # Save/load prepares a new runtime and replaces every owner pointer.
        command(0,8,1);command(0,8,0)
        assert command(5,0,0)==0
        assert imported(header+pcm)==0 and metric(1)==2
        assert command(6,0,0)==0 and metric(1)==1
        command(1,7,1)
        assert lib.fm_sim_render(h,audio,256)==0
        assert max(abs(v) for v in audio[:512])>.001
        command(1,7,0)
        saved=backup();bad=C.create_string_buffer(b'FM1P bad')
        assert lib.fm_sim_restore(h,bad,8)!=0 and backup()==saved
        good=C.create_string_buffer(saved)
        assert lib.fm_sim_restore(h,good,len(saved))==0
        assert metric(1)==1 and backup()==saved
        print('Native bridge PASS: transactional WAV rejection, fast clicks/key release, save/load/runtime rebind, audible restored notes, validated backup replacement.')
    finally:lib.fm_sim_destroy(h)

# USB audio and clock boundary

The device adapter selects one pinned board stack: vendored felucca-bsp `7d8f849`. `chip/usb.c` has optional raw UAC RX packet/reset hooks and full-speed packet-size staging, recorded in its per-file lock with original hashes. MIDI and UAC coexist; CDC/OTA are disabled in the sampler binding. Only 44.1 kHz PCM16 stereo is advertised in this development target. The FM-1 is the peripheral; an iPad/computer is the host.

`workstation/platform/fm_usb.c` validates packet alignment/size and stream/connection generations before writing a bounded stereo ring. It rejects overrun safely and reports counters. Main drains a bounded block outside IRQ time for conversion/capture. Disconnect/lifecycle generations clear FIR chronology and finalize/flag capture loss rather than joining unrelated streams. USB MIDI packet parsing and panic on RX overflow use the same single render owner; input is never automatically echoed.

`workstation/audio/src.c` uses a 32-tap normalized windowed-sinc with sixteen input-frame lookahead and a carried uint64 rational position. Coefficients are computed outside the streaming path; 44.1↔36 kHz and 48→36 kHz have exact cached phases (40/49/3). Other allowed ratios use bounded phase quantization (host 128 phases, board 64), rather than per-frame trigonometry. Input/output chunk size cannot change chronology. Tests check chunk equivalence, an independent direct FIR reference and high-frequency rejection. A single `process` call reports consumed input and produced output; the caller retains unconsumed input.

The audio engine/mixer stays at 36 kHz. Target output converts nominal 36→44.1 kHz before codec DMA. Capture sums L/R at half gain by default, preserving headroom, and stores PCM16 without destructive normalization. The local simulator's test tone models host PCM through SRC/capture; the system suite separately feeds the raw packet parser.

The actual iPad connection, codec clock, feedback/drift strategy, long-term occupancy, USB DMA bank behavior and simultaneous audio/MIDI performance remain device gates. The adapter is compiled, not physically qualified. Nominal rate conversion alone cannot correct an unmeasured physical clock error. Do not infer iPad compatibility from a host WAV or packet test.

The retained `fz_` library separately tests its earlier 16-tap converter and 2^32 chronology regression. It is not the executable board's converter.

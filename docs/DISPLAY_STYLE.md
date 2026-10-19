# Small-screen visual direction

The user's FM1 reference uses bold pixel text, dark surfaces, blue selection bands, green captured slots and amber focus. Preserve that hierarchy across the actual 240×240 display and browser workspaces.

| Role | Color | Use |
|---|---|---|
| Background | `#080910` | Display and graph backgrounds |
| Primary text | `#f5f5ef` | Names, values and white-on-blue text |
| Panel | `#1b2030` | Grouped controls, status and alternating rows |
| Blue | `#294f88` | Selection bands and empty slots/steps |
| Green | `#52cb80` | Filled slots/steps, audio curves and meters |
| Amber | `#f4bf35` | Focus borders, selected values and listening state |
| Secondary text | `#b9c2d5` | Supporting legends and inactive labels |
| Red | `#f27575` | Recording, clipping and errors |

Primary panel labels and values use the original bold 7×10 pixel face; page titles use 10×14 glyphs. The compact 5×7 face is for supporting detail and legends. Shorten supporting instructions before shrinking a primary value. Keep text clear of graph, row and footer boundaries. White and amber have at least 4.5:1 contrast against the selected blue; dark text belongs on filled green cells.

Color accompanies labels, slot numbers, selection borders and state text. Keep those cues. The capture overview keeps sixteen slots, input level, state and the selected setting visible together. Two bold rows show free/total sample memory in green and estimated remaining mono recording seconds / 1-second sample count in amber. Browser capacity cards additionally show 10-second sample estimates, with free slots listed separately, and explicitly label the capacity as an emulator budget rather than measured FM1 memory. HOME keeps the four knob labels and numeric values without parameter bars. Routine OK messages are suppressed on every page; empty status strips are omitted. Useful instructions, save messages and errors remain visible. Existing knob assignments and direct browser actions remain intact. Browser buttons, headings, labels and roll note names use stronger font weights; mobile controls retain their touch targets. Browser rotaries use 48×48-pixel targets and moving amber indicators; drag, wheel and keyboard input replace adjacent +/- buttons.

`fm_ui_palette` owns display colors. The native RGB PNG and WebAssembly RGB transport read it directly. The LCD adapter sends explicit high-byte-first RGB565 scanlines. The indexed framebuffer stays one byte per pixel. Do not add a second full framebuffer just to introduce color.

Check native-size screenshots as well as enlarged screenshots. `make test native-test` covers palette bounds, primary font height, capture states, meter colors, LCD wire bytes and native RGB output. `make web-test` covers the C-to-browser color transport, slot colors, active page indication and touch workflows at iPad/390/320-pixel widths. Physical panel color/readability still requires device validation.

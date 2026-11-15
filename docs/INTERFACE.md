# Audio interface

`firmware/interface/` is host-tested firmware for a stereo router with a step sequencer, an arpeggiator, and retrospective MIDI capture. It is a separate core from the sampler in `firmware/workstation/`. The simulator still runs one workstation engine, and the PI32 image still builds only that sampler. Nothing here is flashed.

The behavior references are Melodee nightly `3fe570d285d1d8da5438d93963c6048734e9e4d2` (2026-11-03, firmware next-3fe570d) and FM-1 Audio Router v0.4.0 `d029a18dd1da660f2200b21a46711e6fe6f4da85` (2026-11-12). Both commits are pinned in `third_party/sources.lock`. Their trees are not in this repository. The equalizer from the router (five bands, four stages, 128-sample delay) is omitted so the sequencer, arpeggiator, and capture fit the router role. Bluetooth codecs are absent. USB class enumeration is absent: the router consumes interleaved stereo frames at the project rate of 36 kHz.

## What the host core does

- Stereo input level, pan, and mute, mixed with one square-wave monitor per track.
- A peak limiter after the mix. An impulse under the ceiling stays a single frame.
- Output modes LOOP (return the input), LOCAL (return the monitor), and MIX (return the limited speaker mix).
- Eight tracks, 64 steps, up to four notes on a step. Default division is 1/16. At 120 BPM that step is 4500 frames, and the clock remainder carries into the next block.
- Arpeggiator modes Off, Up, Down, Up/Down, Random, Order, and Repeat. Sixteen held notes. Hold latches a chord until the next one. Repeat stays on the last note.
- A 128-event retrospective ring for the selected track. Switching tracks starts a new window. A rest longer than eight seconds restarts the free-take buffer. Recording, and an external clock, block that buffer.
- Free-take tempo uses the nightly integer function. Candidates are 16, 32, and 64 steps. The nearest BPM from 40 to 240 wins, a tie keeps the shorter loop, and a candidate within 3% of the current BPM leaves the current BPM in place. One note, or notes already present on another track, keep the current tempo and still capture. Button-down ends the loop; the hold itself is not part of the span.
- An empty pattern becomes that 1, 2, or 4 bar loop and starts playing. A pattern that already has notes keeps its length and merges the latest steps without clearing notes already there.
- Undo and redo put the previous notes and length back. They leave the BPM where the take set it.
- A 240×240 page in the shared palette: blue selection, green meters and filled steps, amber focus and playhead, red REC and clip. Pages are Mix, Seq, and Arp. Mix shows the left and right meters, LIMIT ON, the output mode, and the BPM. Seq shows the first 16 steps. Arp shows the mode, rate, and octave.

Sampler retrospective recording is unchanged. It still ranks timing candidates and does not force every sampler take into 1, 2, or 4 bars. The contract for both clocks is section 27 of `FM1_FZ_INSPIRED_SPEC.md`, beside sections 14.3–14.5.

## Check

```sh
make interface-test
```

That binary is also a prerequisite of `make test`, so `./scripts/check.sh` runs it. The suite includes the nightly BPM cases, the zero-tail impulse, arp order, the capture ring, merge-without-erase, and the 240×240 Mix and Seq pages.

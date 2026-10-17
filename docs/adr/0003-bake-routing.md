# ADR 0003 — Bake and unsend routing

Status: accepted for the host engine
Date: 2026-10-17

## Decision

Sends are post-fader and applied once:

```text
send = post_fader * track_send * region_trim / (127 * 127)
```

A track send of 0 or a region trim of 0 contributes nothing. Trim 127 follows the track send. The default trim on a new region is 127.

Mute forces that voice's dry output and its new sends to 0. A running effect tail is left to decay. A wet stem (`no_send`, or a baked wet asset played beside the dry sample) is never fed into a send.

Region bake:

- The mask is partial when another send on that region is still active. The new asset is stored as `wet_asset`, only the masked trims are zeroed, and the dry asset stays. Playback of the wet asset does not re-enter the rack.
- The mask covers every active send on that region. The region asset becomes dry plus wet, `wet_asset` is cleared, and the masked trims are zeroed.
- Track sends are not changed. Other regions are not changed.

Pattern bake sets `baked`, `unsend_mask`, and `baked_asset` on that pattern only. Region trims and track sends stay. `fz_voice_sends` drops a masked slot for that pattern and keeps it for any other pattern. The stored `baked_asset` is the rendered region. The live voice still plays the original region; the pattern flag only removes the live send.

`fz_bake_shared_sum_is_exact()` returns 0. A shared nonlinear slot is not treated as the sum of solo bakes.

A bake that cannot fit in the PCM store returns before the effect processes the take, so the watermark and the routing stay as they were. Undo of a region bake restores the region fields, including trims and the asset id. It does not rewind `pcm_used`, so the rendered copy can remain until a later compact.

## What the live mixer does

`fz_engine_render` mixes sends only while `fz_engine_set_fx` holds a rack. With no rack, the sample path is unchanged. `fz_performance_render` does not attach a rack. `fz_performance_render_sends` does, then clears it. The return is wet only and is added once.

## Consequences

Baking spends sample storage and can retire a live send. It is not a claim that total RAM went down, and it is not a flash transaction. Four effect buffers in the host rack are host ceilings. `fz_fx_ram_used` counts a buffer only while the slot is enabled or its tail is still running; the static arrays themselves are not returned to the operating system.

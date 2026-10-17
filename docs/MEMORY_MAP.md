# Memory map

Nothing in this file is a linker script for fm1-fz-sampler. No target binary has been linked. Figures below are copied from pinned sources so they are not mistaken for our budget.

## Address window the BSP treats as readable RAM

`fm1_sys.h` `fm1_mem_readable` at felucca-bsp `7d8f849` accepts:

- `0x01C00000 .. 0x01C80000` (512 KiB window)
- `0x02000000 .. 0x02100000` (1 MiB XIP window)

The top of that RAM window is reserved by the same sources: boot mailbox `0x01C7FD80` (`"usb_update_mode"`), update parameter block `0x01C7FD88` for 112 bytes. `fm1_mailbox_clear` covers `0x01C7FD80 .. 0x01C7FE00`.

## Upstream hwtest linker, not ours

`examples/hwtest/app.ld` at the same commit:

| Region | Origin | Length | Role in that script |
|---|---|---|---|
| XIP | `0x02000120` | `0x8DFBC` (~556 KiB) | application code and constants |
| RAMTEXT | `0x01C00000` | `0x6000` (24 KiB) | code that runs with XIP off |
| RAM | `0x01C08000` | 96 KiB | `.data`, `.bss`, general RAM |
| NOINIT | `0x01C7C000` | `0x3D50` | survives reset |

Stacks and a CPU1 reservation are placed by symbols between `0x01C71000` and `0x01C7C000`, not inside the 96 KiB region. There is a gap from the end of that 96 KiB region (`0x01C20000`) up to the CPU1 stacks. The hwtest script does not assign it. This firmware does not assign it either. Using it needs a device-backed map and a recovery plan.

The conversational "about 96 KiB RAM" matches this upstream **section length**, not the whole SRAM window, and not a sample budget. The specification forbids adopting it as our capacity. It is recorded here so later work does not rediscover it as if it were a measurement we made.

## Upstream Felucca flash windows, not ours

`fm1_flash.h` comments describe Felucca's own store (data `0x00097000..0x000E0000`, globals, OTA staging, autosave). Those addresses are Felucca's. This project has no partition table and must not program them.

The comment's expected JEDEC id `0x856014` is an expectation in that header, not an id read from the user's board.

## Official V13 flash API, not a capacity

FM1 Open `sdk/include/fm1.h` at `b178ac4` exposes a user partition whose size is returned at runtime (`fm1_flash_size`). The header says firmware, boot, VM, and BTIF are outside it, sectors are 4096 bytes, and a full instrument already uses that partition for its own format. Their groove project file places samples at offset 69632. That offset is their format. It is not a chip size and it is not a partition this firmware may write. Their audio section attribute `.volatile_ram_code` is how they keep the ISR alive while flash is programmed. This tree still has no flash driver.

## What this repository allocates on the host

Safety ceilings, so a bug stops instead of growing without bound. They are not product limits:

| Ceiling | Value | Meaning |
|---|---|---|
| Engine rate | 36,000 frames/s | Internal render clock |
| Mono PCM16 | 72,000 bytes/s | Raw payload only |
| Voices | 16 | Simultaneous notes in the host engine |
| Instruments, tracks | 8 | Host project object cap |
| Patterns | 32 | Host project object cap |
| Regions / assets | 64 / 48 | Host project object cap |
| Multi slots | 16 | The workflow size, not preallocated audio |
| SRC queue | 4,096 frames | Bounded transport queue |
| History | 512 events | Retrospective MIDI ring |
| Additive table | 1,024 points, 48 levels | Rebuild buffer on the stack, no malloc |

Capture length is whatever buffer the caller passes. A full 16-slot kit is not reserved up front. Empty slots hold no PCM.

## Still unknown

Physical SRAM size beyond the window above, DMA-capable banks, cache geometry, how much of the flash is factory-protected, and how much of the gap above `0x01C20000` can be used without fighting the bootloader. The manufacturer's V13 headers name an ALNK rate of 44118 Hz and an API block of 44100 Hz. Neither has been measured on a unit here. Those stay unknown until a target link map and a non-destructive device read exist.

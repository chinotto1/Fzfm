> Historical M0 decision. The same selected board layer now has a compiled adapter; ADR 0002 and BUILD.md document current implementation and pinned runtime use.

# ADR 0001 — Platform layer and portable engine

Status: accepted for M0/M1
Date: 2026-10-17

## Decision

Build a portable C engine in this repository and test it on the host. Put the FM-1 behind a later adapter based on **felucca-bsp** at `7d8f8490c2245f334775889cb7107573b12289e0` (MPL-2.0). Do not merge Felucca, Melodee, SLOOP, X0X, fm1-base, or FM1 Open into this tree.

The musical engine renders at 36 kHz. USB and the codec stay at a transport rate and cross a streaming converter. The first transport rate to wire, when the USB stack is integrated, is 44.1 kHz, because that is what Melodee's pinned UAC1 descriptor advertises.

## Why this base

- The chip is a JieLi AC79-class pi32v2, not an ARM microcontroller. felucca-bsp is the small board layer that matches that fact and is licensed so it can sit under this GPL-3.0 program.
- Melodee (`84e2d97`) is the receive-path reference: stereo playback into the device at 44.1 kHz with explicit feedback, plus MIDI. It is GPL-3.0. Its descriptor and clocking will be re-read at that commit before any USB code is adapted. It was not copied in M0.
- fm1-base is MPL-2.0 and already has a simulator and USB audio, but it also carries Bluetooth and Wi-Fi, which this product does not need. Starting there would import a larger surface than the sampler requires.
- Official FM1 Open ships an application/SDK split and a native engine whose redistribution terms are not settled for this project. It is not the base.
- Felucca, SLOOP, and X0X remain behavior references. Copying their applications would drag in interaction designs the specification tells us not to inherit.

## What is not decided yet

- The exact felucca-bsp files to compile in, and the JieLi toolchain revision `tools/get_sdk.sh` pins. That waits until a target build machine has the toolchain.
- Which physical control, if any, is SEL. See `docs/CONTROL_MAP.md`.
- Our flash partition layout. Upstream addresses are recorded as upstream facts, not copied into a linker script.

## Consequences

Host tests can prove the 36 kHz core, capture state machine, and input arbiter. They cannot prove USB compatibility, flash safety, display timing, or recovery. `scripts/package_firmware.sh` and `scripts/flash.sh` refuse to run.

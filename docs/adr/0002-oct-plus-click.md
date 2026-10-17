# ADR 0002 — OCT+ is the confirm and undo button

Status: accepted
Date: 2026-10-17

## Decision

The SELECT encoder is turn-only. It has no push, and this firmware does not add one.

The confirm and undo actions are the physical OCT+ button. A logical `FZ_BTN_SEL` remains in the arbiter so host tests can emit the same commands. It is not a panel control.

| Screen | OCT+ tap | OCT+ hold |
|---|---|---|
| HOME | Octave up | Undo. The octave stays |
| Capture | Finish and open HOME. The octave stays, and no second instrument is created | Undo the last slot. Capture stays open |
| Numeric entry or a confirm dialog | Confirm | Cancel. Releasing after the hold does not also confirm |
| STEP or PATTERNS, with no dialog open | Next page | Undo |
| EDIT | Confirm the row. A numeric row opens the keypad | Undo |

HOME on a dialog cancels it and stays in the editor. A later HOME returns to the overview. Hold EDIT opens naming and does not also change the tab. OCT- lowers the octave on HOME and steps the page backward in STEP and PATTERNS.

## Why

The panel map used here (Melodee `panel.c` at `84e2d97`, cross-checked with felucca-bsp `fm1_input.h`) lists SELECT as encoder 0. On this instrument that encoder does not click. OCT+ is a real button and already has a hold.

## Consequences

Tap and hold are mutually exclusive. The hold threshold is 500 ms (`FZ_HOLD_US`). `at_us` is the button-down time, so the hold itself is outside a recorded phrase. Changing screens while the button is down does not create a second tap.

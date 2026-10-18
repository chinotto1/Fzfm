# Agent notes

`FM1_FZ_INSPIRED_SPEC.md` is the product contract. `docs/REQUIREMENTS_STATUS.md` is the implementation record. Update both the code and that table together.

Do not treat upstream RAM or flash addresses as this firmware's map. Do not flash. `scripts/flash.sh` must keep refusing until a recovery path has been used on a real unit and a person has authorized a specific image.

The engine rate stays 36 kHz. Do not "fix" pitch by time-stretching. Do not claim the filter, the four effects, or the USB stack work until tests say so. The retained initial fz_ library has a legacy no-filter regression. The executable fm_ workstation has a real filter with separate stability/response tests; do not confuse these models or remove old assertions.

The workstation maps logical SEL to the physical SCL button, pending panel validation. SELECT remains rotary; never invent a push. The original library retains its abstract unbound SEL.

Host checks: `./scripts/check.sh`. New behavior needs a host test that fails if the behavior is removed. Do not update a golden by deleting the assertion.

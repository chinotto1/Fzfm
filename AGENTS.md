# Agent notes

`FM1_FZ_INSPIRED_SPEC.md` is the product contract. `docs/REQUIREMENTS_STATUS.md` is the implementation record. Update both the code and that table together.

Do not treat upstream RAM or flash addresses as this firmware's map. Do not flash. `scripts/flash.sh` must keep refusing until a recovery path has been used on a real unit and a person has authorized a specific image.

The engine rate stays 36 kHz. Do not "fix" pitch by time-stretching. Do not claim the filter, the four effects, or the USB stack work until tests say so. Cutoff currently does not change audio; the test that locks that is supposed to fail when a real filter arrives, and the status table should change in the same commit.

SEL is unbound on purpose. Do not map it to the SELECT encoder unless a device check shows that encoder has a push.

Host checks: `./scripts/check.sh`. New behavior needs a host test that fails if the behavior is removed. Do not update a golden by deleting the assertion.

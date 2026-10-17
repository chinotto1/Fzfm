# Agent notes

`FM1_FZ_INSPIRED_SPEC.md` is the product contract. `docs/REQUIREMENTS_STATUS.md` is the implementation record. Update both the code and that table together.

Do not treat upstream RAM or flash addresses as this firmware's map. Do not flash. `scripts/flash.sh` must keep refusing until a recovery path has been used on a real unit and a person has authorized a specific image.

The engine rate stays 36 kHz. Do not "fix" pitch by time-stretching. The FZ INSPIRED filter runs from `fz_performance_render`. `fz_engine_render` stays dry unless `fz_engine_set_filter` enables it; the sample-for-sample test on that path has to keep passing. USB is not implemented. Effect CPU cost on the AC79 is unmeasured.

SELECT does not click. Confirm and undo are OCT+. See `docs/adr/0002-oct-plus-click.md`. Do not map a push onto the SELECT encoder.

Host checks: `./scripts/check.sh`. New behavior needs a host test that fails if the behavior is removed. Do not update a golden by deleting the assertion.

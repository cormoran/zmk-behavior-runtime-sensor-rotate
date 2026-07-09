This repository contains the `zmk-behavior-runtime-sensor-rotate` ZMK module:
a runtime-configurable rotary-encoder (sensor) binding behavior with a Web UI,
built on `zmk-module-template-with-custom-studio-rpc` and its **unofficial**
custom ZMK Studio RPC protocol.

## Dev Rules

- When designing a new feature or protocol change (writing a design doc under
  `docs/design/`), read `skills/zmk-module-design/SKILL.md` first. It
  condenses the RPC, settings, and web API surfaces and constraints, so do
  not read dependency sources for design work.
- Before writing or modifying proto, firmware, web, or test code, read
  `skills/zmk-module-dev/SKILL.md`. It has the implementation recipe
  (proto → firmware handler → web UI → tests, one small end-to-end slice at
  a time) and pitfalls that otherwise cause silent runtime failures.
- Commit changes at each milestone. Ensure pre-commit works and never bypass
  pre-commit check.
- Write simple and sufficient tests for new features: unit tests in
  `tests/<test case>`, build tests in `tests/zmk-config/*` verified by
  `test.py`. A hardware-free functional test in `tests/renode/` (booting and
  exercising the real `cormoran_rsr` Studio RPC, the way
  `zmk-module-template-with-custom-studio-rpc`'s own `tests/renode/` does) is
  a planned follow-up for this module, not wired up yet — see
  `docs/design/custom-settings-migration.md` §B.3.
- Storage: this module's persisted rotary bindings live in the
  `zmk-feature-custom-settings` typed registry (see
  `docs/design/custom-settings-migration.md`), not hand-rolled
  `SETTINGS_STATIC_HANDLER_DEFINE` code. Prefer that dependency over adding
  new bespoke setting-save code for future settings too.
- Update README.md properly to guide how to use the module to unfamiliar ZMK
  keyboard users. Keep the guide simple but sufficient!
- Create pull request to origin after finishing the task.

## Commands

Test command usually takes 1min.

```
# Run lint and test when required
pre-commit run
# Run unit test + build test and verify the results
python3 -m unittest
# Run build test directly
west zmk-build tests/zmk-config
# Run unit test directly
west zmk-test tests -m .
# Run web tests
cd web && npm test
```

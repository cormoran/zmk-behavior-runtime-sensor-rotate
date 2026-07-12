# Design: migrate storage to `zmk-feature-custom-settings` + adopt template improvements

Status: design (2026-07-09). **Data-storage** backward compatibility is explicitly **not**
required (old `rsr/s<sensor>/l<layer>` flash records may be abandoned). **BUT the
`cormoran_rsr` Studio RPC proto (`proto/cormoran/rsr/custom.proto`) MUST stay backward
compatible** — do not renumber, remove, retype, or otherwise break any existing field or
message. Existing web/Studio clients built against the current proto must keep working.
The chosen design does not need any proto change (the domain RPC surface is unchanged; only
the storage backing it is swapped), so the default action is: **leave the proto untouched.**
If some ported improvement ever tempts a proto edit, it must be purely additive (new fields
with new tag numbers / new messages), never a breaking change.

This module currently persists rotary-encoder bindings with **raw Zephyr settings**
(`SETTINGS_STATIC_HANDLER_DEFINE` + `settings_save_one("rsr/s%d/l%d", struct, 32)`) and
keeps the authoritative copy in `global_data.bindings[SENSORS][LAYERS]`. We replace that
storage backend with the `zmk-feature-custom-settings` typed registry, and port the
usable subset of improvements that have landed in
`zmk-module-template-with-custom-studio-rpc` since this repo was scaffolded.

Two independent workstreams. Files touched are disjoint, so they can proceed in parallel:
**A. storage migration** (`src/`, `Kconfig`, `west/`), **B. template improvements**
(`web/`, `.github/`, `.claude/`, `skills/`, misc).

---

## A. Storage migration

### A.1 Representation — one BYTES array, element = packed `layer_bindings`

Rejected alternatives (see the investigation reports summarized here):

- *Per-(sensor,layer,direction) typed BEHAVIOR settings + parallel INT32 tap_ms arrays*
  (the runtime-combo pattern). Gives native behavior validation and generic-web-UI
  editing, but: (1) the `BEHAVIOR` value type has **no `tap_ms` field**, forcing a second
  parallel INT32 array per direction (4 arrays total); (2) each array element is a full
  `struct zmk_custom_setting_value` union (~72 B) **regardless of type**, so 4 arrays ×
  N×M elements is ~4× the RAM; (3) the generic-editing benefit is moot because we keep
  our own domain web UI. Not worth it here.

- *Raising `CONFIG_ZMK_CUSTOM_SETTINGS_VALUE_MAX_SIZE`* — unnecessary (see below).

**Chosen (the runtime-macro opaque-blob pattern, adapted to a fixed grid):** a single
`ZMK_CUSTOM_SETTING_ARRAY_DEFINE` of `BYTES` elements. Element `i` holds one
`struct runtime_sensor_rotate_layer_bindings` (`{cw_binding, ccw_binding}`, each
`{uint16 behavior_local_id, u32 param1, u32 param2, u32 tap_ms}`), memcpy'd verbatim.

- `zmk_behavior_local_id_t` is `uint16_t`, so `sizeof(runtime_sensor_rotate_layer_bindings)`
  is 32 B (with padding) — comfortably under the 64 B array-element carrier
  (`CONFIG_ZMK_CUSTOM_SETTINGS_VALUE_MAX_SIZE`, default 64). Add a `BUILD_ASSERT`.
- `tap_ms` rides along for free inside the packed struct — the whole reason to prefer
  this over the typed-BEHAVIOR representation.
- Flat index: `idx(sensor, layer) = sensor * ZMK_RUNTIME_SENSOR_ROTATE_MAX_LAYERS + layer`.
- `_max_count = _default_size = RSR_GRID_SIZE` where
  `RSR_GRID_SIZE = ZMK_RUNTIME_SENSOR_ROTATE_MAX_SENSORS * ZMK_RUNTIME_SENSOR_ROTATE_MAX_LAYERS`.
  Pre-sizing to the full grid means every slot index is valid from boot, so
  `zmk_custom_setting_write_array_by_key(..., idx, ...)` is a plain random-access write
  (no push_back / size bookkeeping).

```c
#define RSR_SUBSYS "rsr"
#define RSR_BINDINGS_KEY "bindings"
#define RSR_GRID_SIZE (ZMK_RUNTIME_SENSOR_ROTATE_MAX_SENSORS * ZMK_RUNTIME_SENSOR_ROTATE_MAX_LAYERS)

BUILD_ASSERT(sizeof(struct runtime_sensor_rotate_layer_bindings)
                 <= CONFIG_ZMK_CUSTOM_SETTINGS_VALUE_MAX_SIZE,
             "layer_bindings must fit one custom-settings array element");

/* All slots default to empty BYTES (size 0) -> get_bindings sees behavior_local_id==0
 * -> DT default-binding fallback applies, exactly as today. Use a range designator, NOT
 * LISTIFY: RSR_GRID_SIZE is a product (SENSORS * LAYERS), and LISTIFY token-pastes its
 * count so it must be a single integer literal; a range bound is just evaluated. A plain
 * {0}-init would leave type==0, an invalid value type. */
static const struct zmk_custom_setting_value rsr_binding_defaults[RSR_GRID_SIZE] = {
    [0 ... RSR_GRID_SIZE - 1] = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_BYTES, .size = 0},
};

ZMK_CUSTOM_SETTING_ARRAY_DEFINE(
    rsr_bindings, RSR_SUBSYS, RSR_BINDINGS_KEY, ZMK_CUSTOM_SETTING_VALUE_TYPE_BYTES,
    RSR_GRID_SIZE, RSR_GRID_SIZE, rsr_binding_defaults,
    ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC,
    ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
    ZMK_CUSTOM_SETTING_NO_CONSTRAINT);
```

### A.2 custom-settings becomes the single source of truth

- **Delete** `global_data.bindings[...][...]`, `SETTINGS_KEY`, `settings_set`, and
  `SETTINGS_STATIC_HANDLER_DEFINE(...)`. custom-settings owns persistence now.
- **Keep** the transient, non-persisted per-(sensor,layer) state in `global_data`:
  `remainder`, `triggers`, `data_accepted`. Unchanged.
- `zmk_runtime_sensor_rotate_set_layer_bindings(sensor, layer, *b)` →
  `zmk_custom_setting_write_array_by_key(RSR_SUBSYS, RSR_BINDINGS_KEY, idx,
   &(struct zmk_custom_setting_value){.type=BYTES, .size=sizeof(*b), .bytes_value=…memcpy…},
   ZMK_CUSTOM_SETTING_WRITE_MODE_PERSIST)`. (Use a local value struct + `memcpy` into
   `bytes_value`; or `zmk_custom_setting_write_bytes` on the element found via
   `zmk_custom_setting_find_array_element`.)
- `zmk_runtime_sensor_rotate_get_bindings(sensor, layer, *out)` → read the element via
  `zmk_custom_setting_read_array_by_key(...)`. If `value.size == sizeof(*out)` memcpy it
  into `*out`; otherwise (empty/unset slot) leave `*out` zeroed. **Then apply the existing
  DT default-binding fallback block unchanged** (the `behavior_local_id == 0` → config
  default logic must be preserved bit-for-bit).
- `..._get_layer_bindings` / `..._get_all_layer_bindings` keep their signatures and just
  delegate to `get_bindings` (they mostly already do).
- The behavior hot path (`_accept_data` / `_process`) currently indexes
  `global_data.bindings[...]` directly. Replace those reads with a call to
  `zmk_runtime_sensor_rotate_get_bindings(sensor, layer, &lb)` into a stack local. A
  rotary rotation is low-frequency, so the extra lock+memcpy is negligible. **Preserve the
  exact triggered-binding / transparent / default-fallback semantics** — this is a
  storage swap, not a behavior change. (Bonus: `_process`'s own duplicated default-fallback
  block can be dropped in favor of `get_bindings`, but only if behavior stays identical;
  if in doubt, leave it.)

### A.3 Keep the domain RPC; suppress self-notifications

We keep the existing `cormoran_rsr` custom Studio RPC subsystem and its domain proto/web
UI (it understands sensors, layers, and CW/CCW — the generic custom-settings web UI does
not). Only the storage under it changes. Because the handler now mutates custom-settings
from inside its own RPC dispatch, it **must** bracket the dispatch with the public
notification-suppress API (else the setting-changed event fires a Studio notification
synchronously on the RPC thread and starves/overflows the in-flight response — issue #38,
hardware-confirmed):

```c
// in template_rpc_handle_request(), src/studio/custom_handler.c
zmk_custom_settings_notify_suppress_begin();
switch (req.which_request_type) { /* … set_layer_cw/ccw_binding, get_*… */ }
zmk_custom_settings_notify_suppress_end();
```

The API is a no-op unless `CONFIG_ZMK_CUSTOM_SETTINGS_STUDIO_RPC` is built. We enable the
generic RPC (see Kconfig) so custom-settings emits proper Studio notifications and offers
generic editing as a fallback — matching the runtime-macro sibling.

### A.4 Kconfig / west / Kconfig.defconfig

`Kconfig` (SR does **not** currently select any of these — add them):

```
config ZMK_RUNTIME_SENSOR_ROTATE
    bool "Enable runtime sensor rotate feature"
    select ZMK_CUSTOM_SETTINGS
    select ZMK_CUSTOM_SETTINGS_ARRAY
    select ZMK_BEHAVIOR_LOCAL_IDS

if ZMK_RUNTIME_SENSOR_ROTATE

config ZMK_RUNTIME_SENSOR_ROTATE_STUDIO_RPC
    bool "Enable runtime sensor rotate custom Studio RPC"
    default y
    depends on ZMK_STUDIO
    select ZMK_CUSTOM_SETTINGS_STUDIO_RPC

endif
```

No `MAIN_STACK_SIZE` change is needed here. `zmk-feature-custom-settings` already raises it
via its own `configdefault MAIN_STACK_SIZE default 2048` (inside `if ZMK_CUSTOM_SETTINGS`),
because Zephyr's `settings_load()` runs on the main thread at boot and its load path
(blob decode / keyspace bind) overflowed the bare 1024 B default on real hardware. Since
this module `select`s `ZMK_CUSTOM_SETTINGS`, that default is inherited automatically — this
is a boot-time settings-load concern on the main thread, unrelated to the Studio RPC
protobuf encode/decode (which runs on the RPC thread), so it cannot be avoided by moving
proto work to another thread.

`west/west-dependency/west-dependency.yml` — add the runtime dependency so dependents pull
it in. Pin to the notify-suppress branch until PR #41 merges (mirror runtime-macro):

```yaml
manifest:
  remotes:
    - name: cormoran
      url-base: https://github.com/cormoran
  projects:
    - name: zmk-feature-custom-settings
      remote: cormoran
      # TODO: revert to `main` once cormoran/zmk-feature-custom-settings#41
      # (public zmk_custom_settings_notify_suppress_begin/end) merges.
      revision: feat/public-notify-suppress
```

`west/west-dependency/west-test-dependency.yml` — add the same project so the module's own
`west zmk-test` build resolves it. **The `zmk` fork revision MUST also be bumped**
`v0.3-branch+custom-studio-protocol` → `main+custom-studio-protocol` (originally thought
out of scope, but it is a hard prerequisite): the 3.5-based fork predates `configdefault`
(Kconfig) and `zephyr_linker_sources(ROM_SECTIONS ...)`, which `zmk-feature-custom-settings`
requires unconditionally, so the module cannot even configure on the old base. Every other
custom-settings consumer (runtime-macro, runtime-combo, template) already pins
`main+custom-studio-protocol`, so this just aligns SR with the ecosystem. Watch for
ZMK/Zephyr 3.5→4.1 API drift in this module's own sensor-behavior code when building.

### A.5 Tests

`tests/studio/` snapshot test must still pass. The stored wire format changes (custom-settings
array records instead of `rsr/s*/l*`), so any snapshot that asserts persisted-key contents
needs regenerating. Verify a full round-trip: set CW/CCW binding via the `cormoran_rsr` RPC
→ read back via `get_all_layer_bindings` → value matches, including `tap_ms`.

---

## B. Template improvements to port ("as feasible")

Ranked; deliver the cheap/independent ones fully, note anything skipped. Full detail lives
in the investigation report; the actionable set:

1. **Bump the web dep pin** `@cormoran/zmk-studio-react-hook` from `#77652d6` to TPL's
   `#8a9903592b61a37bcc209ce8fc17b9d6e545a14b` (adds `useStudioLockState`,
   `isUnlockRequiredError`, `isWebSerialSupported/isWebBluetoothSupported`,
   `useCustomSubsystem`, `connectSerial`). Prerequisite for the web rewrite.
2. **Web UI dual-transport + unlock UX** — port TPL's `web/src/App.tsx` pattern:
   USB (`connectSerial`) **and** BLE (`gattConnect`) buttons gated by
   `isWebSerialSupported()`/`isWebBluetoothSupported()`, `autoReconnect` + prefer
   last-connected serial port, a Bluetooth-advertising hint, a lock-state banner with
   auto-retry-on-unlock, and the template-credit footer. Then give
   `RuntimeSensorRotateConfig.tsx` the same lock-state handling (its RPCs are `SECURED`
   today and currently fail silently while Studio is locked). Keep its existing
   `ZMKCustomSubsystem` encode/decode; migrating to `useCustomSubsystem` is optional
   cleanup, not required.
3. **CI** — copy the composite actions `.github/actions/zmk-env/` and
   `.github/actions/west-init/`; add `.github/workflows/claude.yml` and
   `.github/workflows/devcontainer.yml` (adjust owner/repo strings); update
   `.github/workflows/zmk-module.yml` to add the `pull_request` trigger, the
   `paths-ignore` list, `timeout-minutes: 30`, and the scheduled `keepalive` job. The
   hardware-free **Renode test** (west `zmk-workspace` test dep + a `renode_smoke_test`
   build artifact + `tests/renode/renode_test.py` exercising `cormoran_rsr`) is
   higher-effort — port it but it may land as a follow-up if it balloons.
4. **Agent docs / skills** — replace the stale `.github/agents/*.agent.md` layout with
   TPL's `AGENTS.md` (+ `CLAUDE.md` symlink), `skills/zmk-module-design`,
   `skills/zmk-module-dev`, `.claude/settings.json` + `.claude/hooks/restrict_gh.py`,
   `.codex/environments/environment.toml`. Adapt names/deps to this module. **Do not**
   port `scripts/init_module.py` / `check_placeholders.py` (they are fresh-scaffold
   templating machinery, irrelevant to an already-named module).
5. **Misc, cheap** — `.pre-commit-config.yaml` (`web-build` + `ruff` hooks), `.gitignore`
   reorg, devcontainer `Dockerfile` + `init.sh`, and the `test.py`
   `ConfigAndDeviceTree`/`_test_strings_in_file` refactor. Update `README.md` with the
   Renode section once (3)'s Renode piece lands. Skip HWMv2 board-id renames
   (`seeeduino_xiao_ble`→`xiao_ble//zmk`) unless CI proves the old id no longer resolves.

Out of scope this round (flag, don't do): switching the `zmk` fork branch; the
template-sync placeholder-guard tooling.

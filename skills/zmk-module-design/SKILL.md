---
name: zmk-module-design
description: Architecture reference and design checklist for designing a new feature or protocol change in this ZMK module (custom Studio RPC + web UI, built on zmk-module-template-with-custom-studio-rpc). Read when planning a major feature and writing a design doc under docs/design/ — it condenses the RPC, custom-settings, and web API surfaces plus the constraints that shape protocol design, so a design can be produced without reading the dependency sources.
---

# Design a feature in this module

This file contains everything a design needs from the dependencies. Do not
read `dependencies/zmk/app/include/zmk/studio/custom.h` or
`zmk-feature-custom-settings` headers for design work — the relevant surface
is summarized below. For implementation-time pitfalls, read
`skills/zmk-module-dev/SKILL.md` instead.

## Stack overview

- **Firmware**: a Zephyr/ZMK module implementing runtime-configurable sensor
  (rotary encoder) rotation bindings, plus a custom Studio RPC subsystem
  handler (`src/studio/custom_handler.c`) compiled under
  `CONFIG_ZMK_RUNTIME_SENSOR_ROTATE_STUDIO_RPC`. Messages in
  `proto/cormoran/rsr/*.proto` are compiled by nanopb for firmware and by
  ts-proto for web.
- **Transport**: ZMK Studio RPC (USB serial / BLE GATT) on the patched ZMK
  branch `+custom-studio-protocol`, which adds a custom-subsystem
  multiplexer. This module registers the `cormoran_rsr` identifier; the web
  client discovers subsystems at connect time and exchanges opaque protobuf
  payloads (request/response), plus firmware-initiated notifications.
- **Web**: React + TypeScript (vite) using `@cormoran/zmk-studio-react-hook`.
- **Persistence**: `zmk-feature-custom-settings` (west dependency) provides a
  typed settings registry exposed through its own subsystem
  (`cormoran_custom_settings`) with a generic settings web UI. This module
  keeps its own domain RPC (`cormoran_rsr`) for sensor/layer/CW/CCW-aware
  editing, but stores the underlying bindings as custom-settings array
  elements rather than raw Zephyr settings — see
  `docs/design/custom-settings-migration.md` for the rationale and the
  self-notification-suppression requirement that comes with mutating
  custom-settings from inside your own RPC handler.

## Firmware RPC API surface

- `ZMK_RPC_CUSTOM_SUBSYSTEM(identifier, &meta, handler)` — registers the
  subsystem. `identifier` is a C token (convention `<ns>__<mod>`, here
  `cormoran_rsr`), length bounded by
  `CONFIG_ZMK_STUDIO_RPC_CUSTOM_SUBSYSTEM_IDENTIFIER_MAX_LEN`.
- meta: `ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS("https://...")` plus
  `.security = ZMK_STUDIO_RPC_HANDLER_UNSECURED` (default choice; `SECURED`
  requires the user to unlock, avoid unless the data warrants it).
- handler: `bool handler(const zmk_custom_CallRequest *req, pb_callback_t
  *encode_response)`. Decode `req->payload.bytes` with your nanopb Request
  type; allocate the response via
  `ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER(id, ResponseType)` +
  `..._ALLOCATE`. That buffer is a **single shared static** instance;
  encoding runs **after the handler returns** and possibly multiple times.
  RPCs are serialized — the next request is processed only after the
  response is sent, so one global data buffer per subsystem is safe.
- Notifications (firmware → web, no request):
  `raise_zmk_studio_custom_notification(...)` with the subsystem index and a
  `pb_callback_t encode_payload`. Unlike responses, encoding runs *inside*
  `raise...()`, so stack-local data is allowed there.

## Web API surface

- `useZMKApp()` / `ZMKAppContext`: `state.connection`,
  `findSubsystem("cormoran_rsr")` → `{ index }`, `isConnected`.
- `new ZMKCustomSubsystem(connection, subsystem.index).callRPC(bytes)` →
  `bytes`; encode/decode with the ts-proto generated `Request`/`Response`.
- `zmkApp.onNotification({ ..., callback })` → returns unsubscribe; decode
  the payload with the generated types. Notifications can arrive out of
  order relative to UI state — design handlers to tolerate stale ones.
- Tests: `createConnectedMockZMKApp({ subsystems: ["cormoran_rsr"] })` +
  `ZMKAppProvider` from `@cormoran/zmk-studio-react-hook/testing`.

## Custom settings surface

- One registry entry (or array) per setting:
  `ZMK_CUSTOM_SETTING_DEFINE(c_name, "<subsystem_id>", "key", value_type,
  default, confidentiality, read_perm, write_perm, constraint)` /
  `ZMK_CUSTOM_SETTING_ARRAY_DEFINE(...)`.
  - types: `BOOL` / `INT32` / `STRING` / `BYTES` (value size ≤
    `CONFIG_ZMK_CUSTOM_SETTINGS_VALUE_MAX_SIZE`, default 64 B); arrays
    supported. This module packs each `(sensor, layer)` slot's CW/CCW
    bindings into one `BYTES` element rather than one setting per field —
    see `docs/design/custom-settings-migration.md` §A.1 for why.
  - confidentiality: `DEVICE_PRIVATE` / `RPC_PERSONAL` / `RPC_PUBLIC`;
    permissions: `UNSECURE` / `SECURE` per read/write.
  - constraints: `RANGE` / `OPTIONS` / `HID_USAGE` / `LAYER_ID` /
    `BEHAVIOR_ID` (RANGE has a compile pitfall — see zmk-module-dev).
- C accessors: `zmk_custom_setting_{find,read,write}[_by_key]` (+ array
  variants); write modes `MEMORY` / `PERSIST` / `TEMPORARY`.
- Change event `zmk_custom_setting_changed`
  (`UPDATED`/`SAVED`/`DISCARDED`/`RESET`). It is **not** raised for values
  loaded at boot — plan a boot-time apply path in the design.
- If your own RPC handler mutates custom-settings synchronously (as
  `cormoran_rsr`'s does), bracket the mutation with
  `zmk_custom_settings_notify_suppress_begin/end()` — otherwise the
  self-triggered Studio notification starves the in-flight RPC response on
  the same thread (hardware-confirmed regression; see the migration doc).
- Settings automatically appear in the generic settings web UI. Design
  decision per module: rely on that UI, or build custom controls (this
  module keeps its own sensor/layer-aware controls) that call the same
  registry.

## Constraints that shape protocol design

- One `Request`/`Response` oneof pair per subsystem; all response variants
  share one static buffer sized by the **largest** member — keep every
  message small and similar in size.
- Encoded responses must fit `CONFIG_ZMK_STUDIO_RPC_TX_BUF_SIZE` (default
  64 B!) and requests `..._RX_BUF_SIZE` (default 30 B; 128 needed with
  custom settings). Raising buffers costs RAM permanently. For bulk data use
  the proven patterns instead: paged chunk requests
  (`Get<X>Chunk{id, offset}` → ≤128 B bytes field, `.options`-bounded) or
  notification streaming (one notification per chunk).
- nanopb: no 64-bit field types; every string/bytes field needs a `.options`
  `max_size`; oneof/sub-message handling has pitfalls (zmk-module-dev).
- RPCs are serialized and block the RPC thread: long operations should
  return immediately (kick a work item) and deliver results via
  notifications or polling.
- native_sim testability is a design requirement: RPC/settings must build
  and run with **zero hardware devices** (API stub returning 0 devices), so
  CI covers the protocol without hardware.

## Design checklist (answer these in docs/design/<feature>.md)

1. **Scope**: behavior/feature change, or driver-level (DT bindings,
   `boards/`/`dts/`)? What is explicitly out of scope?
2. **Config surface**: what is compile-time (Kconfig), devicetree, or
   runtime (custom settings)? For each setting: key, type, default,
   constraint, confidentiality, permissions.
3. **RPC API table**: request → response fields and sizes, error cases;
   which calls need chunking or notifications; RX/TX buffer budget with a
   `BUILD_ASSERT` plan.
4. **State & persistence**: persisted vs runtime-only state; boot apply path
   (no changed-event at boot); RAM cost of buffers.
5. **Web UI**: screens, which RPC each uses, generic settings UI vs custom
   controls.
6. **Testing**: native_sim coverage (zero-device stub, test-only init hooks),
   build-test matrix (`tests/zmk-config/build.yaml` artifacts × configs,
   asserted in `test.py`), hardware validation steps.
7. **Phase plan** (proven decomposition — keep the design doc the source of
   truth): a dependency/import step (e.g. adding a west dependency), then
   settings + APIs + RPC (proto → firmware), then web UI (+ streaming), then
   hardware validation.

## File map

| Purpose | Location |
|---------|----------|
| Feature flags, sources | `Kconfig`, `CMakeLists.txt` (proto auto-globbed from `proto/`) |
| Protocol | `proto/cormoran/rsr/*.proto` + `.options` |
| RPC handler | `src/studio/custom_handler.c` |
| Feature code / public API | `src/behaviors/`, `include/zmk/behaviors/runtime_sensor_rotate.h` |
| Bindings, shields (drivers) | `dts/`, `boards/` |
| native_sim tests / build tests | `tests/<case>/`, `tests/zmk-config/` (+ `snippets/`), `test.py` |
| Web UI / tests / codegen | `web/src/`, `web/test/`, `web/buf.gen.yaml` |
| Design docs | `docs/design/*.md` |

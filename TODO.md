# TODO – Hostages Port

## Session tip (gamepkgstd multi-target)

- **Base commit:** `c83882de017a8a3fc198226a4107bdb81d9255b5`
- **Previous tip:** `bb44554` (VERSION / flake / paths / CLI)
- **This tip:** Windows, WASM, Android, R36S/PortMaster, Linux desktop
  integration (`.desktop`, AppStream, icon, man page).

### Packaging status

| Target | Implementation | Verified in-session |
|--------|----------------|---------------------|
| Linux desktop | package + install of desktop/metainfo/svg/man | flake structure only |
| Windows win64/win32 | `nix/windows.nix` + zip | not built here |
| WASM | `nix/wasm.nix`, `Machine::step_frame`, emscripten loop | not built here |
| R36S PortMaster | `nix/r36s.nix` + launcher | not built here |
| Android APK | `nix/android.nix` + `mk/android/` | not built here |

Run on a full machine: `nix build .#hostages-win64-zip`, `.#hostages-wasm`,
`.#hostages-r36s-portmaster-zip`, `.#hostages-android`, `.#checks.version`.

### Remaining packaging

- [ ] Verify each cross target build and smoke-run (ADF required for play)
- [ ] WASM: optional IDBFS mount for persistent saves; file-picker for ADF
- [ ] Android: confirm ndk-build with nested `src/amiga` tree; launcher icons
- [ ] R36S: device test on ArkOS; gptokeyb map if needed
- [ ] CI thin Nix shim for packages + checks

## Gameplay / RE status (unchanged)

Boots from original ADFs; see previous tip. Open RE items: real part I play,
part II to ending, handoff at -13218(a4), music timing, more scenarios.

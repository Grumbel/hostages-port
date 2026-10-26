# TODO – Hostages Port

## Session tip (gamepkgstd packaging)

- **Base commit:** `c83882de017a8a3fc198226a4107bdb81d9255b5`
- **Tip:** packaging alignment with gamepkgstd — VERSION/flake/paths/CLI,
  multi-target (Windows, WASM, Android, R36S), desktop files, `--state-dir`,
  R36S ArkOS wrappers + CXXABI shim, WASM IDBFS, thin Nix CI.

### Packaging status

| Target | Implementation | Verified in-session |
|--------|----------------|---------------------|
| Linux desktop | package + desktop/metainfo/svg/man | structure only |
| Windows win64/win32 | `nix/windows.nix` + zip | not built here |
| WASM | `nix/wasm.nix`, `step_frame`, IDBFS | not built here |
| R36S PortMaster | dualthrust-style sysroot wrappers | not built here |
| Android APK | `nix/android.nix` + `mk/android/` | not built here |

Suggested checks: `nix build .#checks.version`, `.#hostages-win64-zip`,
`.#hostages-wasm`, `.#hostages-r36s-portmaster-zip`, `.#hostages-android`.

### Remaining packaging

- [ ] Verify each cross target build and smoke-run (ADF required for play)
- [ ] WASM: file-picker / preload for ADF; confirm IDBFS on a real browser
- [ ] Android: confirm ndk-build with nested `src/amiga` tree; better icons
- [ ] R36S: device test on ArkOS
- [ ] Optional: expand CI to optional cross jobs (`workflow_dispatch`)

### UI

- [x] Debug menu Save…/Load… submenus (overwrite / New save)

## Gameplay / RE (unchanged)

Open: real part I play, part II to ending, handoff at -13218(a4), music
timing, more scenarios.

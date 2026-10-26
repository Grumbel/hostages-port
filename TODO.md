# TODO – Hostages Port

## Session tip (gamepkgstd packaging alignment)

- **Base commit:** `c83882de017a8a3fc198226a4107bdb81d9255b5`
- **Work line:** bring hostages-port in line with
  [gamepkgstd](https://github.com/Grumbel/gamepkgstd) (DESIGN /
  VERSION-HANDLING / NIX-DEVELOP-SHELL / PACKAGING-CHECKLIST).
- **This tip:** first packaging compliance pass (VERSION, flake, develop
  shell, CLI paths/version, REUSE/GPL license text).

### Gap report (vs PACKAGING-CHECKLIST) — baseline before this tip

| Area | Status | Notes |
|------|--------|-------|
| VERSION file as sole source | **Fail → fixed** | Was hard-coded `0.1.0` in flake only |
| CMake / Nix version propagation | **Fail → fixed** | `PROJECT_VERSION_FULL`, `HOSTAGES_VERSION` |
| `--version` / `--show-paths` | **Fail → fixed** | Required by DESIGN §5 |
| flake authoritative (cleanSource, RelWithDebInfo, no unused deps) | **Fail → fixed** | Removed unused `sdl3`; `checks.version` |
| Nix develop out-of-tree helpers | **Fail → fixed** | `hostages-{configure,build,run,run-gdb}` |
| Path categories (state vs data) | **Partial → improved** | Prefer `XDG_STATE_HOME`; legacy `XDG_DATA_HOME` kept if present |
| `--data-dir` / `--user-data-dir` vocabulary | **Fail → fixed** | Aliases; ADF search roots |
| Support matrix documented | **Fail → fixed** | Linux only; other targets N/A |
| REUSE / SPDX / LICENSE text | **Fail → fixed** | `LICENSES/GPL-3.0-or-later.txt`, `REUSE.toml` |
| Desktop/AppStream/metainfo | **N/A** | Niche RE tool; no store packaging goal yet |
| Windows / WASM / Android / R36S | **N/A** | Not in declared matrix; ADFs not redistributable |
| CI as thin Nix shim | **N/A / Unknown** | No GitHub Actions in tree |
| Full game scenarios / music correctness | **Open** | Unrelated to packaging; see below |

### Remaining packaging work (next)

- [ ] Optional: desktop entry + AppStream if a “playable package” is wanted
- [ ] Optional: Windows cross package (SDL2 only; user supplies ADF)
- [ ] Document config vs state further if host-side settings files appear
- [ ] Wire `nix flake check` into any future CI

## Status (Oct 2026) — gameplay / RE (unchanged intent)

Boots from the original ADFs ("Hostage" trackloader disk and the
"Hostages" AmigaDOS disk) and runs the original code on the emulated A500
(68000 interpreter + chipset + exec/trackdisk/dos/Intuition HLE).

Verified with scripted runs (tests/scenarios):
- bootblock → stage 2 → part I (`apro1.`): intro with music, news ticker,
  manual check (bypassed), rank/mission menu, GIGN picture, briefing,
  plan map, team member selection, street phase (movement, timer)
- stage I completion (forced by pokes) → "Press Return" → helicopter
  flight (graphics.library BltBitMap) → part II (`it.`) with the marksmen
  positions handed over → roof: select HOTEL, choose a roof position,
  descent view
- spotlight collision in the street phase works (timed blitter); see
  docs/spotlight_collision.md for the original bug and a disk patch
- "Hostages": intro, menu (free rank choice), mission, stage I completion
  → helicopter → CLI starts part II → roof (tests/scenarios/hostages)
- --skip-codes: COMMANDER mission on "Hostage" without the access code
- Debug menu (controller Back / F9): save/load states (5 slots), complete
  stage I, soft reset, screenshot, fast-forward toggle
- Gamepad team select (Y / North): stage I only on the plan/map, lists
  DELTA/ECHO/MIKE with status text (in pos / ready / dead / -) and role
  colours; Continue/Restart when targets are set. Stage II lists all six.
  Start/X = Return (continue); F10 restart via the team menu. East (B) =
  Esc (stage II map toggle). Fully playable without a keyboard for
  character switching and stage advance.

## Open (gameplay / RE)

- [ ] Play part I for real (marksmen reach the crosses without pokes)
- [ ] Play part II through rappelling, window breaking and the embassy
      interior to the ending screens (`final.pc1`, "Press F10 to replay
      Part I") and the return to part I
- [ ] Part I→II handoff: it. builds the team table at -32284(a4) by
      unpacking a packed long at -13218(a4) (nibbles → availability of
      DELTA/ECHO/MIKE and assault slots). Trace where -13218 is filled
      (stage-2 loader, shared chip RAM, or a file) — it. does not read
      apro1.'s marksman records directly.
- [ ] Add scenarios for the other missions and the time-out/"team
      decimated" screens
- [ ] Music "a little off" (user report): compare a recording of a real
      emulator with `--wav` output; check CIA-B timer A latch low byte
      (only TAHI is written) and the A500 audio filter model

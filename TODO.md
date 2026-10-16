# TODO – Hostages Port

## Status (Oct 2026)

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


## Open

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
      (only TAHI is written) and the A500 audio filter model. Note the two
      releases have different intro music data.
- [ ] Check whether the spotlight collision bug (docs/spotlight_collision.md)
      exists in "Hostages" too (same street code?)
- [ ] dos.library writes are kept in memory only (no game writes seen)
- [ ] Copper and display are rendered once per frame from the end-of-frame
      state of CPU-written registers; fine for the game's static copper
      lists, revisit if a screen shows tearing or palette splits
- [ ] Mouse (port 1) is wired but unused by the game as far as seen
- [ ] Optional: static recompilation of apro1./it. (both load at $3998;
      select the image by checksum in Machine::program_loaded) and
      hand-translation to C++ as in the FA-18 Interceptor port
- [ ] Optional: CRT-style scaling, stereo crossfeed for hard-panned Paula

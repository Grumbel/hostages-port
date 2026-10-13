# TODO – Hostages Port

## Status (Oct 2026)

Boots from the original ADF and runs the original code on the emulated
A500 (68000 interpreter + chipset + exec/trackdisk HLE).

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

## Open

- [ ] Play part I for real (marksmen reach the crosses without pokes)
- [ ] Play part II through rappelling, window breaking and the embassy
      interior to the ending screens (`final.pc1`, "Press F10 to replay
      Part I") and the return to part I
- [ ] Find out how part I hands its state to part II (it works, but the
      mechanism is not understood: it.'s team table is never written by
      it. itself)
- [ ] Add scenarios for the other missions and the time-out/"team
      decimated" screens
- [ ] Copper and display are rendered once per frame from the end-of-frame
      state of CPU-written registers; fine for the game's static copper
      lists, revisit if a screen shows tearing or palette splits
- [ ] Mouse (port 1) is wired but unused by the game as far as seen
- [ ] Optional: static recompilation of apro1./it. (both load at $3998;
      select the image by checksum in Machine::program_loaded) and
      hand-translation to C++ as in the FA-18 Interceptor port
- [ ] Optional: CRT-style scaling, stereo crossfeed for hard-panned Paula

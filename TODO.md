# TODO – Hostages Port

## Status (Oct 2026)

Boots from the original ADF and runs the original code on the emulated
A500 (68000 interpreter + chipset + exec/trackdisk HLE).

Verified with scripted runs (tests/scenarios):
- bootblock → stage 2 → part I (`apro1.`): intro with music, news ticker,
  manual check (bypassed), rank/mission menu, GIGN picture, briefing,
  plan map, team member selection, street phase (movement, timer)
- part II (`it.`) loads and shows its control screen when started
  directly (HOSTAGES_START_IT), reads keys

## Open

- [ ] Play part I through to the end and verify the switch to part II
      (stage 2 frees apro1 and loads it.) and part II itself, up to the
      ending screens (`final.pc1`, "Press F10 to replay Part I")
- [ ] Find how part I passes its result (team positions, mission) to
      part II; then a test scenario can start part II properly
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

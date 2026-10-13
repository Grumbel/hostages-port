# Stage I: marksmen are shot as soon as they leave cover

## Symptom

In the street phase (placing DELTA/ECHO/MIKE), a marksman is spotted and
shot as soon as he leaves his hiding place, even far away from any
searchlight. The same happens in most Amiga emulators unless the blitter
is emulated cycle-exactly; on a real A500 he is only shot after being
caught in a searchlight.

## How the collision test works

All addresses are runtime addresses in this port (`apro1.` code hunk at
$3998, a4 = $148EE; `tools/disasm.py` produces matching listings). Code
offset = address − $3998.

`$913C` (assembly, called through `$4918` for the active marksman) draws
the marksman with the blitter (mask blit + 3 plane blits; A = mask,
B = bob data, C/D = screen) and then tests him against the searchlights,
which are drawn into the 4th bitplane of the screen:

```
$931A  BLTAPT  = ($10744)          marksman mask      AMOD = d0 (from the plane blits)
$9322  BLTBPT  = a1                4th screen plane at the marksman's position
$932E  BLTCON0 = $0CC0 | shift     USEA|USEB, minterm A·B, no D channel
$9332  BLTCON1 = 0
$9336  BLTSIZE = d4                <-- starts the blit
$933A  BLTBMOD = d1                <-- B modulo (screen modulo) written too late
$933E  jsr $B18E                   wait for the blitter
$9342  d0 = (DMACONR & BZERO) ? 0 : -1
$9350  move.w d0,$10926            "marksman is in a searchlight" (-16328(a4))
```

`BLTBMOD` is written one instruction **after** `BLTSIZE`, so when the blit
starts it still holds the value of the preceding plane blits: d0, the
modulo of the bob data, instead of d1, the modulo of the 320-pixel screen.

The street loop (`$3A5C`…) uses the flag: every loop iteration with
`$10926 == -1` increments a counter (-8(a5)), which is *not* reset when
the marksman leaves the light; at 9 it sets -31894(a4), which makes `$4AFA`
set the "spotted" flag -31892(a4) = 10 (and -31890(a4) = 5). While
spotted, every hit (-16328(a4) ≠ 0) increments field +24 of the
marksman's record (-20902(a4) + 28 × man); when it exceeds field +26 the
marksman is dead (field +18 cleared at `$3B56`).

## Why it works on a real A500 but not in emulators

The blitter adds the modulos at the **end of each row**, using the register
values at that time. On a real A500 the CPU's write to `BLTBMOD` (the next
instruction, 12 CPU clocks) lands while the blitter is still busy with the
first row of the 64-row mask, so every row advances the B pointer by the
correct screen modulo.

An emulator that performs the whole blit at the moment `BLTSIZE` is written
(fast/“immediate blitter” modes, and this port before the fix) uses the
stale modulo for all rows. From the second row on, channel B then samples
the wrong part of the screen; the AND with the mask is practically never
zero, so the marksman is "in a searchlight" all the time. Measured in this
port with the old instant blitter: `$10926` stays `$FF` permanently from the
moment the street phase starts, and the spotted flag is set within about
9 frames of leaving cover. Only cycle-exact blitter emulation reproduces
the real timing and hides the bug.

## Fix in this port

The blitter is no longer instantaneous (`src/amiga/custom.cpp`): a blit
started by `BLTSIZE` runs row by row as CPU time passes (approximate
cycle counts per word by channels used, plus a short start-up delay), registers are
used as they are when each row is done, and `DMACONR` reports BBUSY until
the blit has finished. The late `BLTBMOD` write now lands 8 CPU clocks
after the start, before row 0 has finished, and `$10926` is only set while
a searchlight actually overlaps the marksman.

Check with `HOSTAGES_BLITLOG=1` (logs blitter register writes during an
active blit; in the whole game only `$933A` does this) and
`HOSTAGES_WATCHB=10927` (the in-light flag) during the street phase.

## Patching the original game

Swap the two instructions so the modulo is set before the blit starts.
The bytes are in the code hunk of `apro1.`, which stage 2 loads from the
disk. No checksum over the program was found (the only checksum-like code,
the decryptor at `$6384`, covers the unused track-check routine at `$61DC`
and is never called). Tested in this port with the old instant blitter
(which reproduces the emulator bug): with the patched ADF the in-light flag
toggles normally instead of sticking at `$FF`, and the marksman is spotted
at the same moments as with the timed blitter on the original disk.

| | address | code-hunk offset | ADF byte offset | original | patched |
|-|---------|------------------|-----------------|----------|---------|
| BLTSIZE / BLTBMOD | `$9336` | `$599E` | `$75C6` | `3544 0058 3541 0062` | `3541 0062 3544 0058` |

(`move.w d4,$58(a2)` / `move.w d1,$62(a2)` → `move.w d1,$62(a2)` /
`move.w d4,$58(a2)`.) The ADF offset is the file start (`$1C00`, see the
directory at `$400`) + the hunk file header up to the code payload (`$28`)
+ the code-hunk offset. With that patch the game should behave correctly on
any emulator setting and on fast (accelerated) Amigas, where the CPU's late
write might otherwise also arrive after the first rows.

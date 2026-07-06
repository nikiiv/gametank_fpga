# GameTank MiSTer Core — Testing

## Philosophy

Test the **integration** and the RTL **we write** — not vendored dependencies.
Vendored cores (CPU, VIA) are proven upstream; they get a cheap one-time import
gate, not an ongoing testbench investment. The main effort goes into
**self-written test cartridges whose captured output is verified against the
reference emulator**. Deeper tests are added reactively when a bug earns them.

All simulation is **Verilator** (host-native, no Quartus, no Docker), driven by
`tools/gametank-test`. The reference model is
[GameTankEmulator](https://github.com/clydeshaffer/GameTankEmulator) run
headless — cloned on demand into `sim/GameTankEmulator/` by the test runner
(it is test infrastructure, not vendored code).

## Tiers

### Unit (`gametank-test unit`)

Testbenches only for blocks we author:

- **blitter** — directed cases + randomized ops checked against a small C++
  golden model
- **video timing** — sync/blank positions, vblank IRQ raster position
- **address decode / bank registers** — directed
- **cart controller** — DDR3-latency model, prefetch correctness

Import gate (runs once in CI, not a development area): Klaus Dormann's 65C02
functional suite on the vendored CPU.

### Integration (`gametank-test integration`)

Full-system Verilator sim boots a `.gtr` test cart; the same cart runs in
headless GameTankEmulator. Compared per frame:

- framebuffer CRC (on divergence: both frames dumped as PNGs + diff image)
- audio DAC trace (windowed comparison with tolerance)

Test carts live in `sim/testroms/` and are built with the GameTank SDK
(cc65). Each cart targets one behavior: framebuffer writes, blitter ops,
IRQ timing, pad polling (scripted input injection), audio playback.

### System (`gametank-test system`, opt-in — slow)

Real homebrew games run for thousands of frames with scripted input and
screenshot-hash checkpoints. Run before releases and when chasing
compatibility bugs; not part of every CI run.

### On-hardware (manual)

Smoke checklist per release: boot, pad feel/latency, HDMI + analog video,
audio, OSD load of several games on the DE10-Nano.

## CI

GitHub Actions (`.github/workflows/test.yml`): unit + integration on every
push/PR. Ubuntu runner: apt/verilator + SDL2 + cc65, build headless emulator,
run `tools/gametank-test all`. No Quartus in CI — synthesis is local via
`tools/gametank-build`.

## Adding a test

1. Write a minimal test cart in `sim/testroms/<name>/` (cc65 project, makefile
   produces `<name>.gtr`).
2. Register it in `sim/Makefile` under the right tier with its pass criteria
   (frame CRC checkpoint list, audio window, or C++ assertion harness).
3. If it reproduces a bug: commit the failing test first, then the fix.

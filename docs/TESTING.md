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

First inhabitant (M7): `game_smoke` — a real `.gtr` (path via `GTR_GAME=`)
is pushed through the cart download port and must boot from DDR3 and draw
(`make -C sim system GTR_GAME=/abs/path/game.gtr`). The SDK default
project's logo screen matches the emulator's pixel counts exactly.

Minicraft's M9 title-screen regression runs the real cartridge for 300 raster
frames, then requires 12 captured scanout frames to be byte-identical:
`GTR_GAME=/abs/path/Minicraft.gtr tools/gametank-test system --test minicraft_title`.
The cartridge is external and the test therefore remains opt-in.

### On-hardware (manual)

Smoke checklist per release: boot, pad feel/latency, HDMI + analog video,
audio, OSD load of several games on the DE10-Nano.

## CI

GitHub Actions (`.github/workflows/test.yml`): unit + integration on every
push/PR. Ubuntu runner: apt/verilator + SDL2 + cc65, build headless emulator,
run `tools/gametank-test all`. No Quartus in CI — synthesis is local via
`tools/gametank-build`.

## Sim layout (established in M1)

`sim/Makefile` (driven by `tools/gametank-test`) exposes `unit` /
`integration` / `system` targets. The Verilator top is `rtl/gametank.sv`
itself — the core boundary; `sys/` is never simulated. Each test is a
standalone C++ program (`sim/unit/test_<name>.cpp`,
`sim/integration/test_<name>.cpp`) built on `sim/common/sim_harness.h`,
which clocks the core and samples video exactly as the framework does
(posedge + `ce_pix`), and provides `CHECK`, frame capture, and PPM dumping.
Register new tests by adding the name to `UNIT_TESTS` / `INTEGRATION_TESTS`
in `sim/Makefile`; `--test NAME` filtering maps to `make TEST=NAME`.

## Lockstep determinism rules (learned the hard way in M4)

The emulator faithfully randomizes power-on state (`randomize_memory`,
`randomize_vram`, `srand(time)`), while our RTL resets registers to zero —
so a lockstep cart is only deterministic if it **initializes every register
and memory cell it depends on** ($2005 banking included; its power-on state
is undefined on real hardware) and never reads open bus. Violations show up
as ~50% flaky lockstep runs (the first fb_pattern cart forgot $2005 and
filled the wrong VRAM page whenever the emulator's random banking[3] was 1).
Dynamic carts must also draw every pixel of a frame (clear first) and tag
frames with a multi-byte signature so the comparator can align frame streams
without relying on absolute frame indices (the emulator's 59,659-cycle frame
vs. our 59,474 makes absolute alignment meaningless).

## Audio verification (M6 decision)

The plan called for a lockstep audio-trace comparison "within tolerance",
but the emulator is not a usable audio-timing reference headlessly: in
`--nosound` mode its pacing constants disagree (`clksPerHostSample = 1024`
against `samples_per_frame` calibrated for 256), running the ACP ~4× real
time, and IRQs are quantized to host-sample chunks. The RTL is instead held
to the *schematic*: `sim/unit/test_acp.cpp` asserts exact CD40103 timing
(period = preset+1 at 3.579545 MHz), the DAC zero-order hold, saw
generation via WAI+IRQ firmware, NMI command delivery, and the shared-RAM
path (which is also what lockstep carts exercise functionally). The audible
end-to-end check is the pads_demo boot cart's press-a-button tone on
hardware.

## Adversarial DDR model & gameplay lockstep (M8)

The harness's DDR3 model is deterministic and idealized by default; set
`GT_DDR_HOSTILE=1` (or `GT_DDR_BUSY/LAT/GAPS` individually) for a seeded
adversarial model — variable latency, busy assertion, beat gaps — that
approximates the real HPS port. The Ganymede sprite hunt showed idealized
timing hides real races. Similarly `GT_RDY_GAPS=1` runs the standalone CPU
harness (Klaus, WAI/STP) with random RDY deassertion, modeling the core's
stall behavior.

The emulator lockstep patch supports gameplay lockstep of real games:
`GTE_LOCKSTEP_INPUT="flip:mask,..."` injects pad input at page-flip
indices (same schedule as the sim side — see
`sim/system/test_gany_lockstep.cpp`), and `GTE_ZERO_POWERON=1` zeroes
RAM/VRAM/GRAM and control registers for determinism. Caveat learned the
hard way: the emulator's headless ACP timing is broken (`--nosound` runs
audio off-rate), so games that sync loading/logic to the ACP diverge from
it after the menu — clean-vs-hostile comparison within our own core is
the reliable differential there.

Power-on state is game-visible: Ganymede does thousands of banked-window
reads before its first bank latch, so the cart bank register must power
up matching the emulator (bank 0), not the pull-up value.

Five emulator-floor lessons from the Ganymede and Minicraft flicker hunts,
each with a dedicated regression test:

- **VIA reads are a register file** (`test_via_shadow`): the emulator's
  VIA returns the last written byte on any read — no live timers, no
  IRQs. Ganymede sweeps all 16 VIA registers as boot entropy and reads
  them ~32k times during level load; live via6522 counter values fork
  its procedural level away from the emulator's.
- **Cartridge SPI follows ORA writes, not resolved DDRA pins** (`test_cart`):
  the emulator's `UpdateFlashShiftRegister` explicitly ignores DDRA, and
  the SDK bank routine used by the issue #1 diagnostic does not configure
  it. Feeding the cart from reset-default VIA pins leaves PA0-2 pulled high
  and silently aliases every bank read to bank 0.
- **The banked cart window must be stall-free in steady state**
  (`test_cart_cache`): real mask ROM has no latency. The old 2-slot word
  buffer cost Ganymede's compositor ~7k clk of cart stalls per frame.
  The 16 KB direct-mapped cache makes per-frame tile/sprite re-reads
  free; only first-touch fetches stall.
- **VID_OUT_PAGE is sampled per frame, not muxed live**
  (`test_vidpage_latch`): the emulator presents the page selected by
  $2007 bit 1 once per host frame, so Ganymede's habit of rewriting
  $2007 with the page bit cleared for ~4k CPU cycles mid-frame is
  invisible there. The core copies the requested page into an immutable
  display snapshot during vblank; active scanout never sees the transient.
- **A released frontbuffer cannot leak writes into active scanout**
  (`test_frontbuffer_ownership`): Minicraft legitimately changes $2007
  mid-frame, then selects and redraws the old frontbuffer. Because active
  scanout reads the display snapshot instead of either live VRAM page, the
  released page's clear/redraw stays hidden without a visible page handoff.

The optional real-ROM `minicraft_gameplay` system test navigates New World →
Built in, runs for 15 seconds, requires zero visible snapshot-page handoffs,
and checks at frame 180 that terrain composition has advanced beyond the
uniform green staging frame (the known staging-page hash is gone, frame flips
resumed, and the blitter-write census crossed the known-good threshold).

`test_flash_save_roundtrip` models MiSTer's block-tail timing, where the final
`sd_buff_wr` can overlap falling `sd_ack`, and compares all 2 MB of both the
saved and restored Flash2M image. It also drives hostile idle ioctl data while
the restore finishes, ensuring the shared cart-download mux cannot replace
the final save byte before it reaches DDR.

A caution from the same hunt: with the pre-M8 CPU (no Rockwell bit
instructions) Ganymede's level generation *appeared* chaotically seeded
by input timing — that was derailed execution, not real nondeterminism.
Byte-exact gameplay lockstep against the headless emulator still fails
past the menu for the documented ACP-timing reason, so scene comparisons
remain structural, but treat "the game is nondeterministic" conclusions
with suspicion until the instruction stream is verified (the Klaus
import gate now covers the full W65C02S opcode set, rkwl_wdc_op = 1).

## Adding a test

1. Write a minimal test cart in `sim/testroms/<name>/` (cc65 project, makefile
   produces `<name>.gtr`).
2. Register it in `sim/Makefile` under the right tier with its pass criteria
   (frame CRC checkpoint list, audio window, or C++ assertion harness).
3. If it reproduces a bug: commit the failing test first, then the fix.

# GameTank MiSTer Core — Development Plan

Milestones are sequential; each gates on its acceptance criteria passing in CI
(except explicitly manual items). Scope decisions behind this plan are in
[REQUIREMENTS.md](REQUIREMENTS.md).

## M0 — Research & scaffold ✅ (completed 2026-07-06)

- Hardware research: chip inventory, memory map, blitter/video/audio
  architecture from schematics + emulator source → [HARDWARE.md](HARDWARE.md)
- Dependency selection & licensing audit → [DEPENDENCIES.md](DEPENDENCIES.md)
- Repo skeleton, `tools/gametank-build`, `tools/gametank-test`, CI skeleton
- **Done:** docs complete; dependencies pinned with licenses verified;
  build/test entry points in place. Remaining open questions are tracked
  inside their owning milestones (NTSC line timing & palette variant → M3,
  VIA IRQ wiring → M5, ACP rate encoding → M6).

## M1 — Scaffold builds

- Vendor MiSTer `Template_MiSTer` (`sys/` + `emu` top), rename to GameTank
- Vendor CPU core (per DEPENDENCIES.md decision), VIA source, Klaus binaries
- `tools/gametank-build --no-deploy` produces `output_files/GameTank.rbf`
  showing a test pattern via the framework video path
- `tools/gametank-test` compiles a trivial Verilator sim of the (empty) core
- GitHub Actions workflow runs the Verilator smoke test
- **Done when:** `.rbf` boots to test pattern on the DE10; CI green.

## M2 — CPU & memory

- 65C02 integrated with address decode + BRAM system RAM
- Patch/verify WDC (`WAI`/`STP`) and Rockwell (`BBR/BBS/SMB/RMB`) support per
  the M0 instruction-usage audit
- Klaus 65C02 functional test passes in Verilator (one-time import gate on the
  vendored core, per our testing philosophy — not an ongoing test-development
  area)
- First self-written test cart (built with cc65/GameTank SDK) executes and
  writes a known pattern to RAM; sim asserts on it
- **Done when:** Klaus gate + first test cart pass in CI.

## M3 — Video

- NTSC-timed raster generator (true scanout), dual framebuffers in BRAM,
  front/back flip register, palette LUT
- Framework integration: `ce_pix`, blanking, HDMI via scaler
- Test cart draws a pattern via direct CPU framebuffer writes; sim captures
  the scanned-out frame and CRC-compares against the emulator's frame
- **Done when:** first real pixels over HDMI on hardware; frame-CRC test in CI.

## M4 — Blitter & interrupts

- Blitter per HARDWARE.md register spec; cycle-honest duration & completion IRQ
- Vblank IRQ from raster position
- Emulator-lockstep harness comes online: same `.gtr` runs in Verilator and in
  headless GameTankEmulator; per-frame framebuffer CRCs compared, PNG diffs
  dumped on divergence
- Directed + randomized blitter tests vs a small C++ golden model
- **Done when:** blitter test carts and lockstep runs pass in CI.

## M5 — Input & VIA

- Controller port register block (`$2008`/`$2009` select logic) wired to
  MiSTer joystick inputs (gamepads are **not** behind the VIA)
- 6522 VIA integrated (its cart-banking SPI role becomes live in M7)
- Test cart polls pads; sim injects scripted input and verifies reads
- **Done when:** scripted-input test cart passes; manual pad check on hardware.

## M6 — Audio

- Second 65C02 instance + audio RAM + 8-bit DAC register, per schematic
- Main-CPU↔audio-CPU communication path (shared RAM window, reset control)
- Output through MiSTer audio path (resample to 48 kHz, low-pass)
- Lockstep audio-trace comparison against emulator for an audio test cart
- **Done when:** audio test cart trace matches emulator within tolerance.

## M7 — Cartridge

- DDR3-backed cart controller behind the abstract cart-bus interface;
  prefetch to hide HPS latency (budget: 6502 bus cycle ≈ 280 ns)
- `.gtr` loading via OSD (`ioctl` download path), banking per HARDWARE.md
- **Done when:** first real games boot from OSD-loaded `.gtr` on hardware.

## M8 — Compatibility & release

- Run the known game library + SDK samples through the system suite
  (N-thousand-frame scripted runs, screenshot-hash checkpoints); fix divergences
- OSD polish (config string, reset, aspect options), analog video verified
- Docs finalized; release `.rbf` + submission prep for MiSTer-devel
- **Done when:** library passes; manual on-hardware checklist signed off.

## Post-1.0

- Cartridge flash-write emulation + save persistence to SD (spec already in
  HARDWARE.md)
- Community bug reports; deeper tests added **reactively when a bug earns
  them** — we do not speculatively expand the suite.

## Standing rules

- Test the integration and our own RTL, not vendored dependencies.
- Any emulator divergence found on hardware gets a minimal repro test cart
  before it gets a fix.
- Every vendored-file modification is recorded in DEPENDENCIES.md at the time
  it is made.

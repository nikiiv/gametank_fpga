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

## M1 — Scaffold builds ✅ (completed 2026-07-06)

- Vendor MiSTer `Template_MiSTer` (`sys/` + `emu` top), rename to GameTank
- Vendor CPU core (per DEPENDENCIES.md decision), VIA source, Klaus binaries
- `tools/gametank-build --no-deploy` produces `output_files/GameTank.rbf`
  showing a test pattern via the framework video path
- `tools/gametank-test` compiles a trivial Verilator sim of the (empty) core
- GitHub Actions workflow runs the Verilator smoke test
- **Done when:** `.rbf` boots to test pattern on the DE10; CI green.
- **Done:** system clock 28.636364 MHz (8× colorburst, exact PLL ratio
  63/110); core boundary is `rtl/gametank.sv` (shared by `emu` and the sim
  harness); test pattern verified pixel-identical in Verilator frame capture
  and on-hardware screenshot (`/dev/MiSTer_cmd` load + screenshot).
  Klaus binaries are fetched on demand at the M2 import gate, per
  DEPENDENCIES.md (test infrastructure is not vendored).

## M2 — CPU & memory ✅ (completed 2026-07-06)

- 65C02 integrated with address decode + BRAM system RAM
- Patch/verify WDC (`WAI`/`STP`) and Rockwell (`BBR/BBS/SMB/RMB`) support per
  the M0 instruction-usage audit
- Klaus 65C02 functional test passes in Verilator (one-time import gate on the
  vendored core, per our testing philosophy — not an ongoing test-development
  area)
- First self-written test cart (built with cc65/GameTank SDK) executes and
  writes a known pattern to RAM; sim asserts on it
- **Done when:** Klaus gate + first test cart pass in CI.
- **Done:** CPU patched (WAI/STP/BCD flags + D-clear-on-interrupt, found by
  the Klaus 65C02 gate — see DEPENDENCIES.md §CPU); both Klaus tests pass
  (96.2M / 66.8M cycles); directed WAI/STP tests cover the W65C02S semantics
  Klaus skips. `rtl/mainbus.sv` implements the RDY-gated bus (transactions
  latch at the strobe — DIMUX-derived addresses are only valid then), $2005
  RAM banking (4×8K into 32 KB BRAM), open-bus reads, and the abstract cart
  bus (sim-backed until M7). First cart: `sim/testroms/ram_pattern`.

## M3 — Video ✅ (completed 2026-07-06)

- NTSC-timed raster generator (true scanout), dual framebuffers in BRAM,
  front/back flip register, palette LUT
- Framework integration: `ce_pix`, blanking, HDMI via scaler
- Test cart draws a pattern via direct CPU framebuffer writes; sim captures
  the scanned-out frame and CRC-compares against the emulator's frame
- **Done when:** first real pixels over HDMI on hardware; frame-CRC test in CI.
- **Done:** both M3 open items resolved and recorded in HARDWARE.md §Video —
  palette = the emulator's capture-based table (generated `rtl/palette.sv` /
  `sim/common/gt_palette.h`), raster derived from the Composite_Video_Generator
  schematic (227 CPU cycles/line × 262 progressive lines = 59,474
  cycles/frame; rows line-doubled from line 16, rows 123–127 never scanned;
  NMI at line 0). `rtl/gtvideo.sv` implements it cycle-honestly at the same
  28.636 MHz master clock as the real console, including the 74564
  pixel-latch border behavior. `fb_pattern` cart verifies the full chain
  pixel-exactly in CI (`cart_fb_pattern`), incl. vsync-NMI via a WAI loop.
  The cart is also baked into the `.rbf` as a boot ROM until OSD loading (M7).

## M4 — Blitter & interrupts ✅ (completed 2026-07-06)

- Blitter per HARDWARE.md register spec; cycle-honest duration & completion IRQ
- Vblank IRQ from raster position
- Emulator-lockstep harness comes online: same `.gtr` runs in Verilator and in
  headless GameTankEmulator; per-frame framebuffer CRCs compared, PNG diffs
  dumped on divergence
- Directed + randomized blitter tests vs a small C++ golden model
- **Done when:** blitter test carts and lockstep runs pass in CI.
- **Done:** `rtl/blitter.sv` transcribes the emulator's cycle-exact engine
  (IRQ at exactly W×H cycles, level `pending && COPY_IRQ`, TRIGGER clears);
  12 directed + 150 randomized golden-model sweeps. **The M4 fitting
  decision fell to DDR3**: 512 KB GRAM in BRAM demands 9.7 Mbit vs 5.66
  available, so GRAM lives in HPS DDR3 (`rtl/gram_ddr.sv`: demand-gated
  row-pair prefetch, engine stall on miss, exact IRQ preserved; deviations
  documented in the module header — final build 25% BRAM / 23% logic).
  Lockstep harness: pinned emulator + headless patch, frames aligned by
  cart tag signatures, compared as palette indices. It caught two real cart
  bugs (uninitialized $2005; mirrored-blit GX complement) and two RTL
  prefetcher bugs (over-eager prefetch; serve-slot mux following live tags)
  — determinism rules in TESTING.md. Boot cart = blit_scene; animated
  blitter demo verified on the DE10 over HDMI.

## M5 — Input & VIA ✅ (completed 2026-07-06)

- Controller port register block (`$2008`/`$2009` select logic) wired to
  MiSTer joystick inputs (gamepads are **not** behind the VIA)
- 6522 VIA integrated (its cart-banking SPI role becomes live in M7)
- Test cart polls pads; sim injects scripted input and verifies reads
- **Done when:** scripted-input test cart passes; manual pad check on hardware.
- **Done:** pad ports are hardware-true to `Gamepad_ports.kicad_sch` (one
  7474: read toggles own select FF and clears the other's; D7 = latched
  select state, D6 = extra-button header — both diverge from the emulator,
  which returns constant 1s; lockstep carts mask bit 7). VIA integrated at
  $2800-$2FFF with IRQB wire-ORed onto the CPU IRQ (schematic-confirmed,
  M0 open item closed; cart slot IRQ joins in M7). Scripted-input tests:
  unit (select FFs, cross-reset, VIA register file) + integration
  (pads_demo indicators track injected input through the full video path).
  pads_demo is the boot cart for the manual hardware check.

## M6 — Audio ✅ (completed 2026-07-06)

- Second 65C02 instance + audio RAM + 8-bit DAC register, per schematic
- Main-CPU↔audio-CPU communication path (shared RAM window, reset control)
- Output through MiSTer audio path (resample to 48 kHz, low-pass)
- Lockstep audio-trace comparison against emulator for an audio test cart
- **Done when:** audio test cart trace matches emulator within tolerance.
- **Done:** `rtl/acp.sv` is schematic-true (HARDWARE.md §Audio, M6 open item
  resolved): ACP at CLK14, CD40103 period = {rate[6:0],rate[0]}+1 at
  3.58 MHz (emulator uses preset exactly — <1% pitch divergence, documented),
  AUDIO_RDY as the run gate, DAC zero-order hold, reset released at the next
  tick. The audio-trace lockstep was amended to schematic-exact sim
  verification + a hardware audible check (rationale in TESTING.md — the
  emulator's headless audio pacing runs ~4× real time). Along the way the
  vendored CPU gained the NMI-vs-IRQ-coincidence fix (DEPENDENCIES.md §CPU
  mod 5) — the ACP would otherwise lose main-CPU NMI commands. Boot cart:
  pads_demo now beeps (~424 Hz saw) while any button is held.

## M7 — Cartridge ✅ (completed 2026-07-06)

- DDR3-backed cart controller behind the abstract cart-bus interface;
  prefetch to hide HPS latency (budget: 6502 bus cycle ≈ 280 ns)
- `.gtr` loading via OSD (`ioctl` download path), banking per HARDWARE.md
- **Done when:** first real games boot from OSD-loaded `.gtr` on hardware.
- **Done:** `rtl/cart.sv` (image in HPS DDR3 at `0x3010_0000`, shared with
  GRAM through the new `rtl/ddr_mux.sv` round-robin arbiter; type from
  `.gtr` size; bit-true 74HC595 bank SPI on the VIA Port A pins). Latency
  plan per HARDWARE.md §Cartridge: fixed bank in BRAM (filled once per
  download), banked window from parity-mapped word buffers with a 2-beat
  refill under CPU clock-enable stall — buffers only mutate while the CPU
  is stalled, after an async-prefetch design lost a race between refills
  and hit lookups (caught by `cart_download`; clock-level trace in the M7
  log). Wrapper: OSD `Load Cartridge` (index 1) + `boot.rom` auto-load
  (index 0), console held in reset through the transfer. Tests: `cart`
  unit (Flash2M banking / SPI / fixed bank / sequential refill),
  `cart_download` integration (32 KB EEPROM streamed byte-by-byte, boots
  from DDR3), `game_smoke` system tier (real SDK 2 MB game, logo pixel
  counts match the emulator exactly). Verified on the DE10: SDK game
  auto-boots via `boot.rom` and animates; boot cart still runs when no
  `.gtr` is loaded. **Field fix (same day):** OSD loads froze — the
  wrapper released reset when `ioctl_download` dropped, but the fixed-bank
  fill still had ~30 µs to run, so the console booted the old window and
  had it swapped mid-execution (`boot.rom` loads never hit this: the
  framework's init reset outlives the fill). `dl_wait` now holds reset
  through the fill; repro locked in as `cart_osd_load`, and the pads_demo
  test also asserts the M6 tone through the M7 core. Bad Apple verified
  full-screen on the DE10 via a mid-run indexed load.

## M8 — Compatibility & release (in progress)

- Run the known game library + SDK samples through the system suite
  (N-thousand-frame scripted runs, screenshot-hash checkpoints); fix divergences
- OSD polish (config string, reset, aspect options), analog video verified
- Docs finalized; release `.rbf` + submission prep for MiSTer-devel
- **Done when:** library passes; manual on-hardware checklist signed off.
- **Progress:** Ganymede sprite corruption root-caused and fixed in two
  layers. (1) The blit engine could lag its exact-duration IRQ under DDR
  contention, and IRQ-chained blits restarted the engine over the previous
  blit's tail — fixed by freezing the console clock-enable on engine
  starvation (repro `blit_contention`). (2) The emulator CatchUp()s the
  blitter before `$2005`/`$2007`/param writes, and the SDK's draw queue
  rewrites banking mid-blit relying on it — our live-sampled banking
  re-sourced in-flight blits from the wrong GRAM bank (invisible idle
  sprites, popping textures). Fixed with deferred commits: such writes pend
  while the blit drains on a separate engine enable, CPU stalled (repro
  `blit_bank_race`). Also new: `gram_window_quadrant` locks in the SDK's
  1×1-dummy-blit quadrant-select idiom. See HARDWARE.md §FPGA memory
  budget (M8 amendments).

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

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

## M8 — Compatibility (Ganymede) ✅ (completed 2026-07-08)

Scoped in practice to the one real-world compatibility campaign this
milestone produced: making Ganymede's Climb Race — the most demanding
shipped title — play correctly on hardware. The broader release items
(library sweep, OSD polish, analog video, MiSTer-devel submission) move
to M9 below.

- **Done:** Ganymede plays correctly on the DE10 (user-verified). The
  campaign fixed, in order of discovery, each with a failing-first test:
  blit-engine starvation freeze (`blit_contention`), emulator-matched
  power-on bank 0, pad bit-7 constant, the flash-write command set
  (`flash_persist`), the VIA register-file read shadow (`via_shadow`),
  the 16 KB banked-cart cache (`cart_cache`), the per-frame VID_OUT_PAGE
  latch (`vidpage_latch`), **the missing Rockwell/WDC bit instructions —
  the root cause of the sprite bug** (Klaus extended gate now runs with
  `rkwl_wdc_op = 1`, plus `cpu_bit_ops`/`cpu_zpx`), and GRAM CPU-write
  backpressure (`gram_write_backpressure`, found by the parallel
  `gametank_fix_sprites` investigation).
- **Progress:** Ganymede sprite corruption root-caused and fixed — the blit
  engine could lag its exact-duration IRQ under DDR contention (cart fetches
  vs GRAM prefetch), and IRQ-chained sprite blits restarted the engine over
  the previous blit's tail. Fixed by freezing the console clock-enable on
  engine starvation (`blitter.starved`); repro test `blit_contention`
  (IRQ-chained 4×16 sprites with banked-window execution) failed before,
  passes after. See HARDWARE.md §FPGA memory budget (M8 amendment).
- **Progress:** Ganymede flicker / sprite pop-in root-caused to three
  emulator-floor deviations, each fixed with a failing-first test
  (TESTING.md §emulator-floor lessons): VIA reads now come from a bare
  register-file shadow like the emulator (`via_shadow`; the game seeds
  its level generator from VIA sweeps), the banked cart window got a
  16 KB direct-mapped cache so steady-state re-reads are stall-free like
  real ROM (`cart_cache`; the 2-slot buffer stole ~7k clk/frame from the
  compositor), and scanout latches VID_OUT_PAGE at active-video start
  the way the emulator presents once per frame (`vidpage_latch`; the
  game transiently clears the page bit mid-frame every other frame,
  which a live mux painted as a flickering band of the mid-composition
  page).
- **Progress: Ganymede SOLVED — missing Rockwell/WDC 65C02 bit
  instructions.** The game executes RMB/SMB/BBR/BBS ~1.5M times between
  menu and race; the vendored CPU ran the x7/xF opcode columns as 1-byte
  NOPs, derailing execution into the operand bytes — heroine drawn ~90px
  off (visible only during slash/jump), flickering misplaced sprites.
  Implemented in `rtl/cpu/cpu_65c02.v` (DEPENDENCIES.md §CPU mod 6); the
  Klaus 65C02 extended import gate now runs with `rkwl_wdc_op = 1`,
  exercising all 32 opcodes exhaustively. Post-fix, the sim scanout of
  the Climb Race ready scene matches the emulator's composition
  (heroine center-screen on grass, clean idle-animation deltas). The
  earlier "different procedural level" observations were an artifact of
  the derailed CPU execution, not real seed divergence.
- **Progress (ported from the parallel `gametank_fix_sprites`
  investigation, which independently converged on the same Rockwell-ops
  root cause):** CPU GRAM-window **writes** now backpressure like reads —
  the single pending-write slot could be silently overwritten during an
  HPS DDR busy stretch, dropping bytes from tight sprite-sheet uploads
  (plain `STA abs` streams carry no dummy read to throttle them; 127 of
  128 bytes lost in the repro). `rtl/gram_ddr.sv` stalls the CPU
  clock-enable on a pending write (with an idle fast path) and gains an
  8-word read cache for interleaved sprite-table scans. Tests:
  `gram_write_backpressure` (strengthened to an unrolled `LDA #/STA abs`
  stream — the `STA abs,X` version self-throttled via its dummy read and
  missed the bug), plus directed CPU tests `cpu_bit_ops` and `cpu_zpx`.

## M9 — Library sweep & release

### M9.1 — Flash-save persistence ✅ (2026-07-08)

**Acceptance gate: Ganymede boots into the main game (New Game — not
Climb Race) on hardware, and its flash save survives a core power
cycle.** Hardware gate MET: an automated MGL boot of Ganymede + a
virtual-gamepad Start press enters New Game, which renders the HP HUD
and a live, input-responsive platform level (the main game flash-writes
its save area on entry — it froze there before M8's flash-write set).
Round-trip persistence verified in sim (`flash_save_roundtrip`).

- Flash-save persistence to SD (flash-write *emulation* shipped in M8;
  saves currently live in the DDR3 cart image and die with the session —
  Ganymede's main game saves through it, so it leads M9). Emulator
  behavior: `$90` is the game-issued save trigger; persistence target is
  the MiSTer save-file convention. Design decided 2026-07-08: raw
  full-image `.sav` sized to the cart image (2 MB for Flash2M — the only
  type that can save), streamed DDR3↔SD-block by a new `savectl` ddr_mux
  client; restore = overlay after download + cart-cache sweep. Whole-
  image writes first; dirty-sector tracking (the flash FSM already knows
  touched sectors) only if the ~2 s background save is felt on hardware.
  NOT emulator `.xor`-compatible — decision + reconsideration paths
  recorded in REQUIREMENTS.md §scope cuts
- Run the known game library + SDK samples through the system suite
  (N-thousand-frame scripted runs, screenshot-hash checkpoints); fix
  divergences — the M8 opcode/write-path fixes may change behavior in
  other titles too
- Per-opcode cycle-count census (unit tier): execute every opcode /
  addressing-mode variant on the CPU harness — including page-cross and
  branch-taken/not-taken cases — count RDY strobes, and compare against
  a table extracted from the emulator's `mos6502` cycle counts (the
  compatibility floor games were tuned against). Closes the one detail
  the Klaus suite doesn't check: it validates semantics, not timing —
  a game timing raster/audio loops by instruction cycles could diverge
  undetected today
- OSD polish (config string, reset, aspect options), analog video verified
- Docs finalized; release `.rbf` + submission prep for MiSTer-devel
- **Done when:** library passes; manual on-hardware checklist signed off.

## Post-1.0

- Community bug reports; deeper tests added **reactively when a bug earns
  them** — we do not speculatively expand the suite.

## Standing rules

- Test the integration and our own RTL, not vendored dependencies.
- Any emulator divergence found on hardware gets a minimal repro test cart
  before it gets a fix.
- Every vendored-file modification is recorded in DEPENDENCIES.md at the time
  it is made.

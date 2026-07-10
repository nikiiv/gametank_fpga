# GameTank Hardware Reference

Reverse-engineered from the official sources (verified 2026-07-06):

- **Schematics/BOM:** https://github.com/clydeshaffer/gametank — `Hardware/Combined/` (Prototype3, the production console), `Docs/SchematicPDFs/`, `Docs/BOMs/combined_BOMs.xls`, manual `Docs/gametank-manual-1-06.pdf`
- **Emulator (behavioral reference):** https://github.com/clydeshaffer/GameTankEmulator — file/line citations below are against its `src/`

Where the emulator and schematics disagree, the schematic wins for RTL, but
the emulator defines the compatibility floor (all shipped games were tested
on it).

## Clocks

One crystal domain: **315/88 MHz = 3.579545 MHz** (NTSC colorburst) drives the
main CPU, the blitter (1 pixel/cycle), the audio coprocessor, and video
generation (`timekeeper.h:16`). Vsync every `clk/60` cycles ≈ 59,659 cycles
(`timekeeper.h:23`). In the FPGA everything runs off one system clock with a
3.579545 MHz clock-enable.

## Chip inventory (from combined BOM)

| Part | Role |
|---|---|
| W65C02S6TQG-14 ×2 | Main CPU and audio coprocessor (WDC 65C02, 14 MHz-rated, run at 3.58 MHz) |
| W65C22S6TQG-14 | VIA — Port A bit-bangs SPI to the cartridge bank register |
| SRAM 32K×8 15 ns | System RAM (banked 4×8K window) |
| Dual-port SRAM 32K×8 55 ns | The two 128×128 framebuffers (CPU/blitter side + video scanout side) |
| IDT7134 / CY7C135 (dual-port 4K×8, 20 ns) | Audio RAM, shared main CPU ↔ ACP |
| AS6C4008-55 (512K×8) | GRAM — graphics/asset RAM the blitter reads from |
| 8-bit multiplying DAC + LM358 | Audio output |
| 74HC40103 (down-counters) | Blitter width/height counters |
| 74HC161/163, CD74AC161 (counters) | Blitter VX/VY/GX/GY, video timing |
| 74HC4040 (ripple counter) | Video raster counters |
| 74HC86 (XOR) | Blitter X/Y mirroring (the `~counter` inversion) |
| 74HC595/164 (shift regs) | Cartridge bank register, gamepad interface |
| 74HC139/238, 74AHC30 (decode) | Address decoding |
| 74HC151/157/257 (mux), AC564/573 (latches), HC640 (transceiver), HCS00/HC08/AC32/HC04/HCT74 | Glue |

## Memory map (main CPU) — `gte.cpp:322–355, 440–508`

| Range | Device |
|---|---|
| `$0000–$1FFF` | System RAM window. Physical address = `(banking[7:6] << 13) \| addr` → 4 banks of 8 KB into the 32 KB SRAM (`gte.cpp:188–190`) |
| `$2000–$2007` (writes) | System/audio registers: `$2005` banking, `$2007` DMA control, others → audio registers (decoded `addr & 7`, see Audio) (`gte.cpp:462–507`) |
| `$2008 / $2009` (reads) | Gamepad 1 / 2 (`gte.cpp:348`) |
| `$2800–$280F` | 6522 VIA (`gte.cpp:338–340`). The emulator models it as a bare register file — reads return the last written byte, timers never tick, IRQs never fire — and shipped games depend on that (Ganymede seeds its level generator from VIA register sweeps). The core serves CPU reads from a write-shadow and raises no VIA IRQ (`test_via_shadow`); the real via6522 runs underneath only to drive the Port A cartridge-bank SPI (M8) |
| `$3000–$3FFF` | 4 KB audio RAM, shared with ACP (dual-port) (`gte.cpp:336–337, 461`) |
| `$4000–$7FFF` | VDMA window: framebuffer/GRAM access, or blitter parameters when copy mode is on (see Blitter) |
| `$8000–$FFFF` | Cartridge (vectors at `$FFFA–$FFFF` — **no BIOS/boot ROM**; the console boots straight from cart) |

Reads of write-only ranges return open bus. RAM powers up uninitialized.

### Banking register `$2005` (`system_state.h:9–14`)

| Bits | Function |
|---|---|
| 2:0 | GRAM bank (8 × 64 KB) |
| 3 | VRAM page select for CPU framebuffer access *and* blitter destination |
| 4 | Blitter X clip/wrap disable (`BANK_WRAPX`) |
| 5 | Blitter Y clip/wrap disable (`BANK_WRAPY`) |
| 7:6 | System RAM bank |

### DMA control register `$2007` (`blitter.h:10–17`)

| Bit | Name | Function |
|---|---|---|
| 0 | COPY_ENABLE | 1 = `$4000` window is blitter params & blitter may run; 0 = window is direct framebuffer/GRAM access |
| 1 | VID_OUT_PAGE | Which framebuffer page is scanned out |
| 2 | VSYNC_NMI | Enable NMI on vsync |
| 3 | COLORFILL | Blitter writes constant color instead of GRAM data |
| 4 | GCARRY | 1 = G counters increment across full 8 bits; 0 = low nibble wraps (16×16 tile mode) |
| 5 | CPU_TO_VRAM | 1 = CPU `$4000` window hits framebuffer; 0 = hits GRAM |
| 6 | COPY_IRQ | Enable blitter-completion IRQ |
| 7 | TRANSPARENCY | 1 = write all pixels; 0 = skip writes when pixel data is `$00` (transparent) |

## Video

- Two 128×128×8bpp framebuffers (16 KB each, in one dual-port 32K×8 SRAM).
  CPU/blitter write one page while the other is scanned out (double buffer,
  flipped via VID_OUT_PAGE). **Core deviations (M8/M9):** scanout normally
  samples VID_OUT_PAGE at active-video start instead of muxing it live — the
  emulator presents the selected page once per host frame, so sub-frame
  transients of $2007 bit 1 (Ganymede clears it for ~4k CPU cycles during
  engine housekeeping) are invisible there and must stay invisible here
  (`test_vidpage_latch`). A page cannot remain latched after the game releases
  it for drawing, though: if the CPU or blitter starts writing the current
  scanout page while $2007 selects the other page, scanout immediately hands
  ownership to that selected frontbuffer (`test_frontbuffer_ownership`). This
  prevents Minicraft's old frontbuffer clear/redraw from becoming visible.
  Flips written during vblank (the normal SDK path) still take effect in the
  same frame.
- Output is composite NTSC generated by discrete logic
  (`Hardware/VideoOut/`, 74HC4040 raster counters). 60 Hz, ~59,659 CPU cycles
  per frame.
- 8-bit pixels map to color via a fixed palette — 256×RGB tables in
  `gametank_palette.h` (4 variants). **Resolved (M3, 2026-07-06):** the
  emulator defaults to the **capture-based** palette (`palette.cpp`:
  `palette_select = PALETTE_SELECT_CAPTURE`, entries 256–511 of
  `gt_palette_vals`); our `rtl/palette.sv` is generated from exactly that
  block. Note index 0 is `#1A1A1A`, not pure black (real capture data).
  Emulator display quirk to remember for M4 lockstep: `Palette::ConvertColor`
  remaps any color that lands on pure black to RGB(1,1,1) at render time —
  an SDL artifact, not hardware; frame comparisons should use framebuffer
  indices (or replicate the remap) rather than raw RGB.
- **Resolved (M3, 2026-07-06) — exact raster timing**, derived from
  `Hardware/KiCad/AVBoard_smt/Composite_Video_Generator.kicad_sch` (identical
  counter/decode wiring in the EAGLE `vidgen_only_r3` and KiCad `AVBoard`
  generations). Master clock 28.63636 MHz (8× colorburst); a synchronous
  74AC161 divider produces CLK14/CLK7/CLK3_5, and CPU PH0 = CLK3_5, so raster
  positions are exact CPU-cycle positions. All counts in CLK3_5 cycles:
  - **Line = 227 cycles** (74HC4040 H-counter, CD74HC30 decode of
    2+4+64+128+256 → async HRESET via a CLK7-clocked 7474; one-CLK7 reset
    pulse). 227 is the strongly-favored reading of an async race — the
    alternative is 228; a scope measurement of HRESET on real hardware would
    settle it. HBLANK counts 0–39, HSYNC counts 16–31, colorburst 32–39.
  - **Frame = 262 progressive lines** (V-counter clocked by HRESET, decode
    1+4+256 → VRESET spans line 0) = **59,474 CPU cycles** (60.19 Hz). The
    emulator's `cycles_per_vsync` = 59,659 (exactly 60 Hz) is a whole-frame
    approximation, +185 cycles vs. hardware. VBLANK lines 0–15, VSYNC
    lines 4–7 (CSYNC XNOR serration, no equalization pulses).
  - **Pixels**: XCLK = 28.636/5 = 5.727 MHz; each framebuffer pixel = 2 XCLK
    = 1.25 CPU cycles (2.8636 MHz dot rate). Pixel scan releases at count 63
    (SCAN_DELAY 74HC161 counting 12 × 2H after HBLANK), so 128 pixels span
    counts ≈63–223. First-pixel phase has ±2 CLK28 ripple uncertainty.
  - **Line doubling**: the row counter is clocked by V-count bit 0 and held
    reset by VBLANK; its release swallows one edge, so **row 0 lands on
    lines 16–17**, row r on lines 16+2r/17+2r, and **rows 123–127 are never
    scanned out** (246 active lines, no bottom border).
  - **Borders**: the pixel latch (74564) holds the last latched byte, so the
    left/right borders (counts 40–63 and 223–227) display **pixel 127 of the
    previously scanned row** (previous frame's (127,122) on line 16), not
    black. True black only during H/V blanking (picture-enable off).
  - **Vsync NMI**: ~VRESET crosses to the logic board as VNMI; NMI = VNMI
    gated by DMA flags bit 2 (open-drain into W65C02S NMIB). The NMI edge
    fires **at the start of line 0**, 16 blanked lines (3,632 CPU cycles)
    before scanout resumes at line 16. **No CPU-readable vblank flag
    exists** — vblank is observable only via the NMI.

## Blitter — `blitter.cpp`, `blitter.h`

Parameter registers (in the `$4000` window while COPY_ENABLE=1, decoded
`addr % 8`, `blitter.h:47–54`):

| Reg | Name | Notes |
|---|---|---|
| 0 | VX | Destination X start |
| 1 | VY | Destination Y start |
| 2 | GX | Source X start (GRAM) |
| 3 | GY | Source Y start (GRAM) |
| 4 | WIDTH | Bits 6:0 width; **bit 7 = X mirror** (source read direction) |
| 5 | HEIGHT | Bits 6:0 height; **bit 7 = Y mirror** |
| 6 | TRIGGER | Bit 0 starts the copy; writing also clears the pending blitter IRQ (`blitter.cpp:22–26`) |
| 7 | COLOR | Fill color, stored **inverted** — hardware writes `~COLOR` (`blitter.cpp:108`) |

Behavior (`Blitter::CatchUp`, cycle-accurate model in the emulator):

- **1 pixel per 3.58 MHz cycle**; completion IRQ fires after `W × H` cycles
  (`blitter.cpp:27–28`). This is the timing games race against — must be
  cycle-honest in RTL.
- Source reads: GRAM address = `banking[2:0] << 16 | GY[7] << 15 | GX[7] << 14
  | GY[6:0] << 7 | GX[6:0]` — counter bit 7s select the 16 KB quadrant within
  the 64 KB GRAM bank (`blitter.cpp:112–115`). Mirroring XORs the counters
  (74HC86 on real hardware).
- GCARRY=0 makes GX/GY increment only their low nibble (wraps within a 16×16
  tile); GCARRY=1 increments the full byte (`blitter.cpp:43`).
- Destination writes are suppressed when: pixel is `$00` and TRANSPARENCY=0,
  or the destination counter is off-screen (bit 7 set) and the corresponding
  WRAPX/WRAPY bank bit allows clipping (`blitter.cpp:120–124`).
- COLORFILL substitutes `~COLOR` for GRAM data (solid fill).
- Writing TRIGGER with 0 cancels/clears the IRQ state.

Additional findings (M4, 2026-07-06, from `blitter.cpp`/`gte.cpp` re-read):

- **IRQ is level-shaped:** line = `pending && COPY_IRQ(dma[6])`, with the
  gate evaluated live ($2007 writes can mask/unmask a pending IRQ,
  `gte.cpp:494`); pending sets at copy completion and clears on **any**
  TRIGGER write (`blitter.cpp:22–25`).
- **Mirroring is the full 8-bit complement** of GX/GY applied before the
  GRAM address is formed — the inverted bit 7s participate in quadrant
  selection (`blitter.cpp:107–119`).
- **`gram_mid_bits`** (`{GY'[7], GX'[7]}` of the *mirrored* counters, latched
  at every blitter pixel op): these same lines select which 16 KB quadrant of
  the 64 KB GRAM bank the **CPU's $4000 window** sees when CPU_TO_VRAM=0
  (`gte.cpp:238,263–264`). Software must run a (dummy) blit whose GX/GY high
  bits point at a quadrant before poking it through the window.
- **Window reads while COPY_ENABLE=1 return open bus** (`gte.cpp:225–227`;
  the emulator returns random — real hardware floats the bus).
- Engine pipeline per CPU cycle (`CatchUp` phases): W decrements, then
  V/G counters increment (GX per pixel, GY per row, both through the GCARRY
  rule), row/copy completion evaluates, then the pixel write strobes. INIT
  (the cycle after TRIGGER) loads all counters and writes the first pixel at
  (VX,VY) in that same cycle, so pixels land on cycles 1..W×H after trigger
  and the IRQ arrives at exactly W×H. Degenerate loads: H=0 completes
  immediately (no writes); W=0 behaves as a column writer in the emulator's
  engine while the scheduled IRQ still fires at 0 — software never uses
  either; RTL follows the engine.

## Audio coprocessor — `audio_coprocessor.cpp/h`

- Second 65C02 executing from the shared 4 KB audio RAM (`$3000–$3FFF` on the
  main CPU; the ACP sees it as its entire address space, mirrored — vectors
  included, `audio_coprocessor.cpp:9–15, 86–89`).
- ACP writes with A15 set load the **8-bit unsigned DAC register**
  (`ACP_MemoryWrite`, `audio_coprocessor.cpp:99–104`).
- Main-CPU registers (in `$2000–$2007`, `addr & 7`, `audio_coprocessor.cpp:17–42`):
  - reg 0 (`$2000`): ACP reset (takes effect at next sample tick)
  - reg 1 (`$2001`): pulse ACP NMI
  - reg 6 (`$2006`): rate — bit 7 = ACP running; the register value sets the
    IRQ period in ACP clocks (emulator encodes `irqRate = value<<1 | value&1`);
    sample rate = 3.579545 MHz / period
- The ACP receives a periodic IRQ at the sample rate; its firmware (uploaded
  by the game into audio RAM) computes one sample per IRQ, idling in
  `WAI`-loops (`gametank_sdk/src/gt/audio/audio_fw.asm:51`).
- Analog path: 8-bit multiplying DAC + LM358 low-pass.
- **Resolved (M6, 2026-07-06,** `AVBoard_smt/signals_4.kicad_sch`**):**
  - **ACP clock = CLK14 (14.318 MHz)** — `CLK14_AUD → U1.PH0-IN`; the
    emulator's `clkMult = 4` is schematic-true.
  - **Rate encoding is literal wiring:** the `$2006` value latches into
    `AUD_FLAGS` (74573) whose outputs feed the CD40103 sample down-counter
    presets shifted by one — P7..P1 ← D6..D0 and **P0 ← D0 as well** —
    i.e. period = `(value<<1 & $FE) | (value & 1)` counted at
    **3.579545 MHz** (`CLK3_5_AUD → CP`); the counter self-reloads at TC
    (`TC → PE`) — and counts preset..0 **inclusive**, so the true sample
    period is **preset + 1** clocks (the emulator's accumulator uses exactly
    `preset`: a <1% pitch divergence at typical rates, same class as the
    227-line raster approximation). Bit 7 (`AUDIO_RDY`) never reaches the
    counter — it is the ACP's run gate (RDY).
  - **TC (~AUDIO_IRQ) drives three things:** the ACP IRQB, the reset
    flip-flop's CLR, and the **AD7524 DAC's ~WR** — the DAC re-latches the
    ACP's output byte (staged in the `AUDIO-DAC-BUF` 74573 on ACP A15
    writes) once per sample tick: a zero-order hold at the sample rate.
    The emulator updates its `dacReg` immediately on the ACP write; the
    RTL models the hardware's two stages.
  - **ACP reset**: `$2000` writes preset the 7474 (RESB asserted) and
    master-reset the 40103 (counter → $FF); the next TC clears the FF,
    releasing RESB — "reset takes effect at the next sample tick" ✓.

## Gamepads — `joystick_adapter.cpp:81–91`

SMS/Genesis-style DB9 pads with a select line, read at `$2008` (pad 1) /
`$2009` (pad 2). Each read returns active-low button bits and **toggles that
pad's select flip-flop**; reading one port resets the other's toggle. Bit
layout per read:

| Select LOW | Select HIGH |
|---|---|
| bit 4 = A, bit 5 = Start | bit 0 = Right, bit 1 = Left, bit 4 = B, bit 5 = C |
| bit 2 = Down, bit 3 = Up | bit 2 = Down, bit 3 = Up |

**Schematic detail (M5, 2026-07-06, `LogicBoard_smt/Gamepad_ports.kicad_sch`):**
one 7474 holds both select FFs: `~GAMEPAD1R` clocks FF1 (D=~Q toggle) *and*
clears FF2; `~GAMEPAD2R` symmetric. A 74573 per pad latches the byte at the
read strobe: D0←DB9.4 (Right), D1←DB9.3 (Left), D2←DB9.2 (Down), D3←DB9.1
(Up), D4←DB9.6 (A/B), D5←DB9.9 (Start/C), **D6 = "extra button" header**
(3.3k pull-up — reads 1 with nothing fitted), **D7 = the select FF's Q,
latched before the end-of-read toggle** (first read after the cross-reset
returns select=0 data with bit7=0, then alternates). **Emulator divergence:**
the emulator returns bit 7 (and 6) constantly 1 (`joystick_adapter.cpp` never
sets mask bits 7/15 despite its own comment). RTL implements hardware truth;
lockstep input carts must mask bit 7 before drawing.

## Cartridge — `gte.cpp:275–330, 380–460`

Formats the emulator recognizes (`.gtr` = raw ROM dump, type inferred from
size): 8 KB EEPROM, 32 KB EEPROM, **2 MB flash** ("Flash2M"), 2 MB flash +
32 KB onboard save RAM ("Flash2M_RAM32K").

Flash2M mapping:

- `$C000–$FFFF`: fixed, the **last** 16 KB of flash (holds vectors)
- `$8000–$BFFF`: banked 16 KB window, bank = `bank_mask[6:0]`
- `bank_mask[7]` = 0 on the RAM32K variant maps the 32 KB save RAM into the
  window instead (`bank_mask[6]` picks the half)
- The 8-bit bank register is a 74HC595 shift register loaded by the **VIA
  Port A** via bit-banged SPI: PA0 = clock, PA1 = data, PA2 = latch (also
  flash CS), PA7 = MISO (`gte.cpp:183–186, 275–295`)

Flash command behavior modeled by the emulator (`gte.cpp:440–465` area):
byte program via `$A0` prefix (AND-writes one byte), sector erase `$30`
(non-uniform sector table: 31×64 KB + 32 KB + 2×8 KB + 16 KB at the top),
chip erase `$10`, `$90` treated as the save-to-disk trigger. **Phase 1 is
read-only** ([REQUIREMENTS.md](REQUIREMENTS.md)); this section is the spec
for the post-1.0 save milestone.

**Implemented (M7, `rtl/cart.sv`):** image in HPS DDR3 at byte
`0x3010_0000` behind a round-robin DDRAM arbiter shared with GRAM
(`rtl/ddr_mux.sv`); type inferred from `.gtr` size like the emulator:
≤32 KB = EEPROM mirrored end-aligned across the window (for
power-of-two sizes exactly `addr & (size-1)` — 8/16/32 KB carts all
exist in the wild), else Flash2M (RAM32K deferred post-1.0 with flash
writes). The 74HC595 shift register is modeled bit-true on the VIA Port A
pins (shift on PA0 rise with pre-edge PA1, latch on PA2 rise, bit 7
forced high). **Power-on value (M8):** undefined on real hardware, but
game-visible — Ganymede issues ~3,200 banked-window reads before its
first SPI latch. The emulator's BSS-zero init shows those reads bank 0;
the register powers up as `$80` (bank 0) to match, not the pull-up
pattern. Latency: $C000–$FFFF is copied to BRAM once per download
(zero run-time DDR traffic where the vectors and hot loops live); the
banked window runs from a 16 KB direct-mapped cache (2048×64 data +
2048×9 tag BRAMs, indexed by image word address so bank switches need
no flush), refilled two words per miss under a CPU clock-enable stall
(~30 clk per 16 sequential first-touch bytes — documented timing
deviation, same class as the GRAM CPU window). Steady-state re-reads —
a game's per-frame tile/sprite fetch loop — are stall-free like real
mask ROM (`test_cart_cache`; the earlier 2-slot buffer stole ~7k
clk/frame from Ganymede's compositor, M8). Flash program/erase evicts
the written word's line; a new download sweeps the whole tag store.

## Interrupts

| Line | Source |
|---|---|
| Main CPU IRQ | Blitter completion (enabled by DMA bit 6; cleared by TRIGGER writes). **Resolved (M5):** net `~IRQ` on `LogicBoard_smt/CPU_and_address_decode` wire-ORs (3.3k pull-up, open-drain) the **VIA IRQB**, the **cartridge slot's _IRQ pin (21)**, and the CPU's IRQB — so VIA and cart interrupts share the line with the blitter. The emulator never asserts a VIA IRQ (bare register file), so no shipped software depends on VIA timers. |
| Main CPU NMI | Vsync (enabled by DMA bit 2) |
| ACP IRQ | Periodic sample-rate counter |
| ACP NMI | Main CPU write to `$2001` |

## FPGA memory budget

**Resolved (M4, 2026-07-06):** GRAM in BRAM does **not** fit — the framework
(ascal line buffers etc.) already holds 188 of 553 M10K blocks, and total
demand came to 9.7 Mbit vs 5.66 available. GRAM therefore lives in **HPS
DDR3** at byte base `0x3000_0000` (`rtl/gram_ddr.sv`): a blit's entire GRAM
access set is deterministic at TRIGGER, so a demand-gated prefetcher streams
each row's two gx-quadrant halves (2×128 B) into a double row-buffer one row
ahead; the engine stalls on misses with a self-healing direct fetch. The
completion IRQ stays exactly W×H (independent duration counter). **Amended
(M8):** engine starvation now freezes the console clock-enable outright
(`blitter.starved`), because "mid-blit slip is unobservable" broke down
once the M7 cartridge shared the DDR3 port: narrow-tall sprite blits
starve faster than the IRQ counter runs, and a game triggering the next
blit from its IRQ handler (the SDK pattern) restarted the engine over the
previous blit's still-draining tail — clipped/garbled sprites (Ganymede;
repro locked in as `blit_contention`). With the freeze, the IRQ counter
and engine can never diverge; the console time-base stretches while the
raster keeps real time — same documented-deviation class as the CPU
GRAM-window and cart-miss stalls. System RAM 32 KB + framebuffers
32 KB + audio RAM 4 KB stay in BRAM (M4 build: 25% BRAM, 23% logic).
The M7 cartridge shares the DDR3 client.

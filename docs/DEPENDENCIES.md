# Dependencies & Provenance

Policy ([REQUIREMENTS.md](REQUIREMENTS.md) N3): all synthesizable third-party
code is **vendored** into this repo with upstream URL, exact commit, license,
and every local modification recorded here. Test infrastructure is cloned on
demand, never vendored. Audited 2026-07-06.

## Vendored RTL

### MiSTer framework (`sys/`, `Template.sv` → `GameTank.sv`, Quartus project)
- **Upstream:** https://github.com/MiSTer-devel/Template_MiSTer
- **Commit:** `0eccddb6d7708157291bdcfbcc8a2c7f4a829d41` (2026-07-01)
- **License:** GPL-2.0 top-level; per-file: `sys/hps_io.sv` GPL-3.0, `sys/ascal.vhd` permissive (temlib). Combined work distributed under GPLv3.
- **Rules:** `sys/` is never modified; refreshed wholesale from upstream. Core files renamed Template→GameTank.
- **Vendored set (M1, 2026-07-06):** `sys/` wholesale (unmodified); `Template.qpf/.qsf/.sdc/.srf` → `GameTank.qpf/.qsf/.sdc/.srf`; `Template.sv` → `GameTank.sv`; `files.qip`; `rtl/pll.qip`, `rtl/pll.v`, `rtl/pll/` (the core PLL IP — `sys/pll_q17.qip` hard-references `rtl/pll.qip`, so its location is fixed). Not copied: `Template_Q13.*` (Quartus 13 variants), the `rtl/` demo core (`mycore.v`, `lfsr.v`, `cos.sv` — replaced by our own `rtl/testpattern.sv`), `clean.bat`, `Readme.md`.
- **Local modifications:**
  1. `GameTank.qpf`: `PROJECT_REVISION` "Template" → "GameTank". `.qsf/.sdc/.srf` content unchanged.
  2. `GameTank.sv`: `emu` body rewritten for GameTank (config string, `rtl/gametank.sv` instantiation); framework boilerplate (port include, unused-port defaults, hps_io/PLL/video hookup) retained from Template.
  3. `files.qip`: rewritten to list GameTank core files.
  4. `rtl/pll/pll_0002.v`: `output_clock_frequency0` 20 MHz → **28.636364 MHz** (8× NTSC colorburst; 50 MHz × 63/110 — exact on Cyclone V, VCO 1575 MHz). Matching cosmetic edit to the retrieval info in `rtl/pll.v`.

### CPU — 65C02 (`rtl/cpu/cpu_65c02.v`, `rtl/cpu/ALU.v`)
- **Upstream:** https://github.com/hoglet67/verilog-6502 (Arlet Ottens' 6502 core + 65C02 extensions by David Banks & Ed Spittles)
- **Commit:** `ef2cc5ab453b0c35e8c9f459b52eb72be70b71d7` (2025-12-14 — includes the RDY/DIHOLD and TSB/TRB-with-RDY fixes)
- **License:** permissive attribution notice (Arlet Ottens): *"Feel free to use this code in any project (commercial or not), as long as you keep this message, and the copyright notice."* GPLv3-compatible; headers preserved.
- **Why not Arlet's dedicated `verilog-65C02-microcode`:** it carries **no license grant** (all rights reserved by default) — unusable in a GPLv3 repo without written permission; also fewer field deployments and a non-W65C02S cycle profile.
- **Vendored (M1, 2026-07-06):** `cpu_65c02.v`, `ALU.v` copied unmodified, upstream headers preserved.
- **Local modifications (M2, 2026-07-06 — all in `cpu_65c02.v`, marked "GameTank mod"):**
  1. **WAI** ($CB): new state `WAI0` (6'd54); decode entry placed before the
     NOP1 catch-all. Stalls with the bus parked until `IRQ | NMI_edge`; the
     existing DECODE interrupt logic then either vectors (I=0 IRQ, NMI) with
     return address = instruction after WAI, or falls through to the next
     instruction (IRQ with I=1) — W65C02S semantics. `SYNC` asserts on wake
     (REG-like).
  2. **STP** ($DB): new state `STP0` (6'd55), self-loops until reset.
  3. `IMPLEMENT_CORRECT_BCD_FLAGS` enabled (W65C02S-accurate BCD N/Z).
  4. **D flag cleared at BRK3** — the 65C02 clears decimal mode when taking
     BRK/IRQ/NMI/reset; the vendored core didn't (NMOS behavior). Found by
     the Klaus 65C02 extended test (BRK pass 2, `$ff-decmode` flag check).
  5. **NMI edge preserved across coincident IRQ entry (M6):** upstream
     cleared `NMI_edge` at any BRK3, so an NMI arriving during an IRQ's
     interrupt sequence was silently lost — the GameTank ACP hits this
     constantly (sample-rate IRQs + main-CPU NMI commands). The sequence now
     records at BRK2 whether it took the NMI vector and only then consumes
     the edge (`NMI_taking`, marked "GameTank mod").
  6. **Rockwell/WDC bit instructions `RMB/SMB/BBR/BBS` implemented (M8):**
     the earlier "no GameTank software uses them" assumption was falsified
     by Ganymede, which executes them ~1.5M times between menu and race
     (hot code copied to RAM `$0200-$0340`); worse, `IMPLEMENT_NOPS` ran
     the whole x7/xF columns as **1-byte** NOPs, so execution derailed
     into the instruction operands (the M8 "sprites in wrong positions"
     bug — the W65C02S and the emulator both implement these). RMB/SMB
     ($x7) ride the existing zero-page read-modify-write path (`ZP0 →
     READ → WRITE`) with a decoded one-hot mask operand into the ALU
     (`rsb_ins`, `rsb_mask`; AND/~mask for RMB, OR/mask for SMB; no flag
     updates, W65C02S-correct 5 cycles). BBR/BBS ($xF) add states `BBR0`
     (issue zp address) and `BBR1` (latch the tested bit from the read
     data, consume the rel byte) and then reuse the stock branch datapath
     (`BRA0/BRA1/BRA2`) with the latched bit as the condition (`bbx_ins`,
     `bbx_cond`; 5 cycles + 1 taken + 1 page-cross, W65C02S-correct).
     Opcode fields are latched at DECODE (`bit_sel`, `bit_set`) — `IR` is
     only the opcode during DECODE in this core. All marked
     "GameTank mod".
- **Import gate (sim/, runs in CI):** Klaus `6502_functional_test.bin` stock
  (success `$3469`, ~96.2M cycles) + 65C02 extended test reassembled from the
  amb5l CA65 port @ `966b1a35049f9d8be44ad092ec6d43d5ba1831b3` with
  `rkwl_wdc_op = 1` (Rockwell bit ops exercised exhaustively — M8; success
  label planted at build time, ~66.8M cycles).
  Note: `wdc_op = 1` only makes the extended test *skip* $CB/$DB (Klaus
  covers WAI/STP in the separate interrupt test, which has no CA65 port) —
  actual WAI/STP semantics are covered by our directed test
  (`sim/unit/test_cpu_wai_stp.cpp`): IRQ-wake without vectoring (I=1),
  IRQ-wake with vectoring and resume (I=0), NMI wake, STP halt/reset-revive.

### 6522 VIA (`rtl/via/via6522.v`)
- **Upstream:** https://github.com/harbaum/nanomac — `src/macplus/via6522.v`, Till Harbaum's Verilog port of **Gideon Zweijtzer's** heavily reverse-engineered 6522 (1541 Ultimate lineage; accuracy refinements credited to gyurco/Gyorgy Szombathelyi).
- **Commit:** `87a358424e26b456578a3a643774fc0b968d55bd` (2025-09-30)
- **License:** GPL 3.0 (stated in file header) — clean match for this repo. Note: *older* copies of Gideon's VIA (e.g. the X-HDL translation floating around in some cores) carry a "do not copy without written permission" header — do **not** substitute one of those.
- **Survey result (2026-07-06):** evaluated Thomas Skibo's `via6522` (BSD-3, PET2001 lineage — solid, simpler; our fallback), MikeJ's `m6522` Verilog port (BSD-like, VIC-20/fpgaarcade lineage, used in hoglet's ice40 Atom), and misc smaller cores. Gideon's wins on accuracy pedigree + native-Verilog + GPL3.
- **GameTank usage floor** (from GameTankEmulator `src/gte.cpp`): the emulator models the VIA as a bare register file with Port A bit-banged SPI (CLK=PA0, MOSI=PA1, CS=PA2, MISO=PA7) clocking the **cartridge bank shift register** on Flash2M carts; no timers/SR/IRQ are emulated, so no shipped software can depend on them. We vendor the full chip anyway (real hardware has it, cost is nil).
- **Vendored (M1, 2026-07-06):** `src/macplus/via6522.v` → `rtl/via/via6522.v`, upstream header preserved.
- **Local modifications (M5, 2026-07-06):** `latch_reset_pattern` changed from
  `wire` to `localparam` (Quartus 17 rejects non-constant declaration
  initializers; value unchanged). The file must also be compiled as
  SystemVerilog (`files.qip`) — it uses continuous assigns to variables.
- **Integrated (M5):** at $2800–$2FFF (`addr & 15`), phi2 mapped to the CPU
  cycle (`falling` = the RDY strobe), IRQB wire-ORed onto the CPU IRQ per the
  schematic finding in HARDWARE.md §Interrupts. Port pins loop back the
  VIA's own drive (pull-ups on inputs) until the M7 cart SPI takes Port A.

### DDR3 helper (`rtl/cart/ddram.sv`)
- **Upstream pattern:** Sorgelig's `ddram.sv` as used in https://github.com/MiSTer-devel/AtariLynx_MiSTer (`rtl/ddram.sv`) — the reference implementation for OSD-download-to-DDR3 cart storage.
- **License:** GPLv3 (© Sorgelig 2019).
- **Commit:** recorded at vendoring time (M7).

## Test infrastructure (cloned on demand, not vendored)

### GameTankEmulator (reference model)
- **Upstream:** https://github.com/clydeshaffer/GameTankEmulator — C++/SDL2, GNU make.
- Cloned on demand by `sim/Makefile` into `sim/GameTankEmulator/` (gitignored),
  pinned to `e7e25e2daf5da5d041ae8dc48f740a362c1e66ff` (2026-06-03); submodules
  `src/mos6502` + `src/imgui/ext/implot` at the recorded gitlinks.
- **Headless (M4):** `sim/emulator-patches/lockstep-headless.patch` adds
  env-driven per-frame framebuffer dumps (`GTE_LOCKSTEP_OUT`,
  `GTE_LOCKSTEP_FRAMES`) at the vsync boundary in `mainloop`; runs under
  `SDL_VIDEODRIVER=dummy` with `--softrender --nosound --nojoystick` (no xvfb
  needed). Dumps are 16 KB of palette indices per frame (the displayed page),
  avoiding the palette-RGB quirk entirely.

### Klaus Dormann 65C02 functional tests
- **Upstream:** https://github.com/Klaus2m5/6502_65C02_functional_tests @ `7954e2dbb49c469ea286070bf46cdd71aeb29e4b` — **GPLv3**.
- Stock `6502_functional_test.bin` used as-is (success PC `$3469`, entry `$0400`).
- 65C02 extended test must be **reassembled** with `rkwl_wdc_op = 0` (stock binary requires Rockwell ops) and, once WAI/STP are patched in, `wdc_op = 1` so they are exercised; use amb5l's CA65 port (https://github.com/amb5l/6502_65C02_functional_tests). Pass/fail: PC repeating at same sync address; success address per the `.lst`.
- Role: one-time import gate on the vendored CPU (see [TESTING.md](TESTING.md)) — not an ongoing test area.

### GameTank SDK (test-cart toolchain)
- **Upstream:** https://github.com/clydeshaffer/gametank_sdk — requires **cc65 built from git snapshot** (release 2.19 is too old), Node.js, zopfli, GNU make. `CFLAGS --cpu 65c02`, `AFLAGS --cpu W65C02`.

## Tools (not in repo)

| Tool | Version / source | Role |
|---|---|---|
| Quartus Prime | 17.0.x via Docker `raetro/quartus:mister` (mirror: `ghcr.io/raetro/quartus:17.0`; fallbacks: `ryanfb/quartus-mister`, `JupSys/quartus-mister`) | Synthesis (`tools/gametank-build`) |
| Verilator | 5.x, one pinned version for local + CI (brew bottles 5.048; ubuntu-24.04 apt ships 5.020 — pin and match) | All simulation |
| cc65 suite | git snapshot build | Test carts via GameTank SDK |
| SDL2 | system package | Reference emulator build |
| Docker | any recent | Quartus containment |
| JimmyStones/Verilator_Template | https://github.com/JimmyStones/Verilator_Template | Reference pattern for the sim harness (sim-top instantiating core only; C++ plays the hps_io/ioctl role). Built on Verilator 4.204 — expect API adjustments on 5.x. |

## Reference-only (read, not copied)

- https://github.com/MiSTer-devel/AtariLynx_MiSTer — closest analog: small console, `ioctl` → `ddram.sv` cart load. Primary structural reference.
- https://github.com/MiSTer-devel/NeoGeo_MiSTer — multi-channel DDR3 with per-channel caching, if sustained cart bandwidth ever needs it.
- GameTank hardware repo + emulator source — authoritative behavior reference (see [HARDWARE.md](HARDWARE.md)).

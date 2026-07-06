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
- **Local modifications:** none yet (M1 will record the rename set here).

### CPU — 65C02 (`rtl/cpu/cpu_65c02.v`, `rtl/cpu/ALU.v`)
- **Upstream:** https://github.com/hoglet67/verilog-6502 (Arlet Ottens' 6502 core + 65C02 extensions by David Banks & Ed Spittles)
- **Commit:** `ef2cc5ab453b0c35e8c9f459b52eb72be70b71d7` (2025-12-14 — includes the RDY/DIHOLD and TSB/TRB-with-RDY fixes)
- **License:** permissive attribution notice (Arlet Ottens): *"Feel free to use this code in any project (commercial or not), as long as you keep this message, and the copyright notice."* GPLv3-compatible; headers preserved.
- **Why not Arlet's dedicated `verilog-65C02-microcode`:** it carries **no license grant** (all rights reserved by default) — unusable in a GPLv3 repo without written permission; also fewer field deployments and a non-W65C02S cycle profile.
- **Planned local modifications (M2, each recorded here when made):**
  1. Add **WAI** ($CB) — hard requirement: GameTank SDK `wait()`, text rendering, and audio firmware idle loops use it. Must implement W65C02S semantics incl. IRQ-with-I=1 resuming without vectoring.
  2. Add **STP** ($DB) — SDK exposes `stop()`; unused in known software but cheap.
  3. Enable `IMPLEMENT_CORRECT_BCD_FLAGS` (off by default; needed for W65C02S-accurate BCD N/Z and the Klaus 65C02 test).
  4. Rockwell `BBR/BBS/SMB/RMB`: **not implemented** (no GameTank software uses them; they run as correct-length NOPs via `IMPLEMENT_NOPS`). Post-1.0 fidelity item.

### 6522 VIA (`rtl/via/via6522.v`)
- **Upstream:** https://github.com/harbaum/nanomac — `src/macplus/via6522.v`, Till Harbaum's Verilog port of **Gideon Zweijtzer's** heavily reverse-engineered 6522 (1541 Ultimate lineage; accuracy refinements credited to gyurco/Gyorgy Szombathelyi).
- **Commit:** `87a358424e26b456578a3a643774fc0b968d55bd` (2025-09-30)
- **License:** GPL 3.0 (stated in file header) — clean match for this repo. Note: *older* copies of Gideon's VIA (e.g. the X-HDL translation floating around in some cores) carry a "do not copy without written permission" header — do **not** substitute one of those.
- **Survey result (2026-07-06):** evaluated Thomas Skibo's `via6522` (BSD-3, PET2001 lineage — solid, simpler; our fallback), MikeJ's `m6522` Verilog port (BSD-like, VIC-20/fpgaarcade lineage, used in hoglet's ice40 Atom), and misc smaller cores. Gideon's wins on accuracy pedigree + native-Verilog + GPL3.
- **GameTank usage floor** (from GameTankEmulator `src/gte.cpp`): the emulator models the VIA as a bare register file with Port A bit-banged SPI (CLK=PA0, MOSI=PA1, CS=PA2, MISO=PA7) clocking the **cartridge bank shift register** on Flash2M carts; no timers/SR/IRQ are emulated, so no shipped software can depend on them. We vendor the full chip anyway (real hardware has it, cost is nil).
- **Local modifications:** none planned.

### DDR3 helper (`rtl/cart/ddram.sv`)
- **Upstream pattern:** Sorgelig's `ddram.sv` as used in https://github.com/MiSTer-devel/AtariLynx_MiSTer (`rtl/ddram.sv`) — the reference implementation for OSD-download-to-DDR3 cart storage.
- **License:** GPLv3 (© Sorgelig 2019).
- **Commit:** recorded at vendoring time (M7).

## Test infrastructure (cloned on demand, not vendored)

### GameTankEmulator (reference model)
- **Upstream:** https://github.com/clydeshaffer/GameTankEmulator — C++/SDL2, GNU make.
- Cloned by `tools/gametank-test` into `sim/GameTankEmulator/` (gitignored), pinned to a commit recorded in `sim/Makefile`.
- **Caveat:** no headless mode upstream — the lockstep harness carries a small headless/trace patch (kept in `sim/emulator-patches/`), or CI falls back to xvfb.

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

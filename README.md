# GameTank for MiSTer

FPGA implementation of the [GameTank](https://gametank.zone/) — Clyde
Shaffer's open-source 8-bit game console (W65C02S @ 3.58 MHz, blitter-driven
128×128 dual-framebuffer video, 6502-based audio coprocessor) — as a core for
the [MiSTer](https://mister-devel.github.io/MkDocs_MiSTer/) platform
(DE10-Nano). No SDRAM add-on required.

**Status: M1 (scaffold & first `.rbf`) — test-pattern core builds and passes CI; next up M2, CPU & memory.** See
[docs/DEVELOPMENT_PLAN.md](docs/DEVELOPMENT_PLAN.md) for the milestone
roadmap, [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md) for scope and fidelity
targets, and [docs/HARDWARE.md](docs/HARDWARE.md) for the reverse-engineered
hardware reference.

## Repository layout

```
docs/            requirements, hardware reference, dev plan, testing, dependencies
rtl/             core RTL (SystemVerilog; vendored CPU/VIA in rtl/cpu, rtl/via)
sys/             MiSTer framework (vendored from Template_MiSTer — never edited)
sim/             Verilator testbenches, test cartridges, lockstep harness
tools/           gametank-build (synthesis+deploy), gametank-test (test suite)
.github/         CI — unit + integration tests on every push
```

## Building the core

Requires Docker (Quartus runs containerized — no local Quartus install).

```sh
./tools/gametank-build --no-deploy   # compile only → output_files/GameTank.rbf
./tools/gametank-build               # compile + deploy to $MISTER_HOST
./tools/gametank-build --no-hdmi     # faster dev compile (MISTER_DEBUG_NOHDMI)
./tools/gametank-build --clean       # wipe Quartus build dirs first
```

Deploys via ssh/scp to `MISTER_HOST` (default `root@mister.local`):
dev builds to `/media/fat/_GameTankDev/` (timestamped + `GameTank.rbf`
latest), official-style builds to `/media/fat/_Console/`.

## Running the tests

Host-native, no Docker/Quartus needed:

```sh
./tools/gametank-test            # unit + integration
./tools/gametank-test unit
./tools/gametank-test integration
./tools/gametank-test system     # slow: full-game runs, opt-in
./tools/gametank-test all --test blitter_fill   # single test
```

See [docs/TESTING.md](docs/TESTING.md) for the test-cart / lockstep-versus-
emulator approach.

## Tools used

Full provenance (exact commits, licenses, local patches) lives in
[docs/DEPENDENCIES.md](docs/DEPENDENCIES.md).

| Tool | Purpose |
|---|---|
| [Quartus Prime 17.0.x](https://www.intel.com/content/www/us/en/software-kit/669513.html) via [raetro/quartus:mister](https://hub.docker.com/r/raetro/quartus) Docker image | FPGA synthesis for the DE10-Nano (Cyclone V) |
| [Verilator](https://www.veripool.org/verilator/) 5.x | All simulation and the CI test suite |
| [MiSTer Template_MiSTer](https://github.com/MiSTer-devel/Template_MiSTer) | Core scaffold + `sys/` framework (HPS I/O, ascal scaler, audio out) |
| [hoglet67/verilog-6502](https://github.com/hoglet67/verilog-6502) (`cpu_65c02.v`) | 65C02 CPU core (Arlet Ottens / David Banks / Ed Spittles) — patched with WAI/STP for W65C02S |
| [via6522 by Gideon Zweijtzer](https://github.com/harbaum/nanomac) (Verilog port: Till Harbaum) | 6522 VIA |
| [ddram.sv (Sorgelig)](https://github.com/MiSTer-devel/AtariLynx_MiSTer/blob/master/rtl/ddram.sv) pattern | HPS DDR3 cartridge storage |
| [GameTankEmulator](https://github.com/clydeshaffer/GameTankEmulator) | Reference model for lockstep integration tests |
| [GameTank SDK](https://github.com/clydeshaffer/gametank_sdk) + [cc65](https://cc65.github.io/) (git snapshot) | Building test cartridges |
| [Klaus Dormann's 6502/65C02 tests](https://github.com/Klaus2m5/6502_65C02_functional_tests) | One-time CPU import gate |
| [JimmyStones/Verilator_Template](https://github.com/JimmyStones/Verilator_Template) | Reference pattern for the MiSTer Verilator harness |
| [GameTank hardware repo](https://github.com/clydeshaffer/gametank) | Schematics/BOM — authoritative hardware reference |
| Docker, GNU make, GitHub Actions | Build containment, test driving, CI |

## Credits

- **Clyde Shaffer** — the GameTank console, emulator, and SDK
- **Arlet Ottens, David Banks, Ed Spittles** — 65C02 CPU core
- **Gideon Zweijtzer, Till Harbaum** — 6522 VIA core
- **Sorgelig / MiSTer-devel** — the MiSTer framework
- **Klaus Dormann** — 6502/65C02 functional tests

## License

GPLv3 (see [LICENSE](LICENSE)). Vendored files retain their upstream
copyright headers; see [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md).

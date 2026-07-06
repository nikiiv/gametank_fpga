# GameTank MiSTer Core — Requirements

Decisions below were fixed during the research-phase interview (2026-07-06).

## Goal

A MiSTer (DE10-Nano) FPGA core for the [GameTank](https://gametank.zone/), the
open-source 8-bit console by Clyde Shaffer, faithful enough that every known
GameTank game and SDK sample behaves identically to real hardware.

## Fidelity target

- **Functional accuracy**, validated against
  [GameTankEmulator](https://github.com/clydeshaffer/GameTankEmulator) as the
  reference model.
- **Cycle-honest where software can observe timing**:
  - blitter operation duration and completion IRQ timing,
  - vblank timing (derived from real raster position, not a synthetic timer),
  - audio coprocessor synchronization with the main CPU.
- Full bus-level cycle accuracy is explicitly **not** a goal.

## Functional requirements

| # | Requirement |
|---|-------------|
| R1 | Emulate the GameTank main CPU (W65C02S @ 3.579545 MHz), including every CMOS/Rockwell/WDC instruction that shipped GameTank software uses (audit gate: Klaus 65C02 suite + instruction-usage survey of SDK and games). |
| R2 | Emulate the 6522 VIA as wired on the real board — its primary system role is Port-A bit-banged SPI driving the cartridge bank shift register; full chip vendored regardless. |
| R3 | Emulate the blitter and dual 128×128 framebuffer video system with **true scanout**: an NTSC-timed raster generator reads the front framebuffer and feeds the MiSTer video pipeline (`ce_pix` → ascal scaler → HDMI; analog out via the framework). Vblank IRQ derives from raster position. |
| R4 | Colors are produced by a palette LUT validated pixel-for-pixel against emulator screenshots. |
| R5 | Emulate the audio coprocessor faithfully: a second 65C02 instance with its own RAM and 8-bit DAC, exactly as on the schematic; output resampled through the standard MiSTer audio path. |
| R6 | Load `.gtr` cartridge images (up to 2 MB) via the MiSTer OSD file picker into HPS DDR3. |
| R7 | Map MiSTer joysticks/gamepads to the GameTank controller registers (`$2008`/`$2009` select logic — **not** the VIA). |
| R8 | Standard MiSTer conveniences: reset from OSD, aspect-ratio / scaling options via the framework, config string with sensible defaults. |

## Non-functional requirements

| # | Requirement |
|---|-------------|
| N1 | All new RTL in **SystemVerilog**; whole system simulable with **Verilator** (no VHDL in the synthesis/simulation path). |
| N2 | **No SDRAM add-on required.** Internal memories in BRAM; cartridge in HPS DDR3 behind an abstracted cart-bus interface (latency hidden by prefetch; SDRAM backend possible later without touching the system). |
| N3 | Third-party RTL is **vendored** with provenance (upstream URL, commit, license, local patches) recorded in [DEPENDENCIES.md](DEPENDENCIES.md). |
| N4 | Test suite runs on the host with `tools/gametank-test` (Verilator, no Quartus/Docker); synthesis with `tools/gametank-build` (Docker `raetro/quartus:mister`). CI (GitHub Actions) runs unit + integration tiers on every push. |
| N5 | License: **GPLv3** for the repo; vendored files keep their upstream headers. |

## Phase-1 scope cuts (deliberate)

- **Cartridge is read-only.** Flash-write emulation (command set of the real
  flash part) and save persistence to SD are specified in
  [HARDWARE.md](HARDWARE.md) but implemented post-1.0.
- No "no cartridge inserted" behavior beyond a sane idle screen.
- Exotic dev-hardware variants (early board revisions) are out of scope; we
  target the current production hardware revision.

## Definition of done (1.0)

Every known released GameTank game and the SDK sample set runs with no
user-visible divergence from GameTankEmulator, verified by the integration
suite (framebuffer CRC / screenshot checkpoints, audio trace comparison) and a
manual on-hardware smoke checklist (input feel, HDMI + analog video, audio).

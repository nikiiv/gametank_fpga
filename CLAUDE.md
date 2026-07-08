# GameTank MiSTer Core

FPGA implementation of the GameTank (open-source 8-bit console: W65C02S @
3.579545 MHz, discrete blitter, 128×128 dual framebuffers, 6502 audio
coprocessor) as a MiSTer core for the DE10-Nano. No SDRAM add-on — cart in
HPS DDR3, everything else in BRAM.

**State: M0–M8 complete (Ganymede compatibility campaign done). Next: M9 — library sweep & release prep.**

## Read these before writing RTL

| Doc | What it holds |
|---|---|
| [docs/DEVELOPMENT_PLAN.md](docs/DEVELOPMENT_PLAN.md) | Milestones M0–M8 with acceptance gates; M1 checklist is there |
| [docs/HARDWARE.md](docs/HARDWARE.md) | Full reverse-engineered spec: memory map, banking/DMA registers, blitter (cycle-level), audio, cart flash, gamepads — with emulator file:line citations |
| [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md) | Fidelity targets and scope cuts (what is deliberately NOT built) |
| [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md) | Pinned upstream commits + licenses for everything we vendor; update it with every vendored file or local patch |
| [docs/TESTING.md](docs/TESTING.md) | Test strategy and how to add tests |

## Hard rules

- All new RTL is **SystemVerilog** and must simulate under **Verilator**
  (no VHDL in the core path; `sys/` is exempt — it is never simulated).
- `sys/` (MiSTer framework) is vendored wholesale and **never edited**.
- Vendored third-party RTL: record upstream/commit/license/patch in
  DEPENDENCIES.md **in the same commit**.
- Test the integration and our own blocks — **not** vendored cores. No
  speculative test suites; a bug earns its test (failing test first, then fix).
- Timing that software can observe must be cycle-honest: blitter duration
  (1 px/cycle, IRQ at W×H), vblank from real raster position, ACP sync.
- CPU cores get patched for **WAI/STP** (GameTank software requires WAI —
  details in DEPENDENCIES.md §CPU).

## Host environment

Development started on an Apple Silicon Mac — **synthesis does not work
there** (the `raetro/quartus:mister` Docker image is x86_64-only; Quartus
17.0 has no ARM build). Simulation/tests run anywhere. This repo is meant to
live on an **x86_64 Linux box** for M1 onward.

Linux box setup:

```sh
# Synthesis
docker pull raetro/quartus:mister        # Quartus 17.0.x, ~5.7 GB
# Simulation & tests
apt install verilator make g++ libsdl2-dev libsdl2-image-dev
# Test carts (cc65 release 2.19 is too old — build from git)
git clone https://github.com/cc65/cc65 && make -C cc65 && ln -s $PWD/cc65/bin/* ~/.local/bin/
apt install nodejs zopfli                # GameTank SDK asset pipeline
# Deploy target: ssh access to the MiSTer (default root@mister.local,
# override with MISTER_HOST). Default password on stock MiSTer: 1
```

## Additional Tools

If tools are needed to continue development, pick the best one for Linux x64
and if not present ask the user to install it. Check the user's Linux flavour
and suggest the best command to do so. Verify the OS variant at the beginning
of a session.

## Commands

```sh
./tools/gametank-build --no-deploy   # Quartus compile in Docker → output_files/GameTank.rbf
./tools/gametank-build               # compile + scp to $MISTER_HOST (dev: /media/fat/_GameTankDev)
./tools/gametank-build --no-hdmi     # much faster dev compile
./tools/gametank-test                # Verilator unit + integration suites (no Docker/Quartus)
./tools/gametank-test system         # slow full-game runs, opt-in
```

CI (GitHub Actions) runs `gametank-test all` on every push; Quartus is never
run in CI.

## Starting M1 (do these in order)

1. Vendor https://github.com/MiSTer-devel/Template_MiSTer at commit
   `0eccddb6d7708157291bdcfbcc8a2c7f4a829d41`: copy `sys/`, `Template.qpf/.qsf/.sdc/.srf`,
   `Template.sv`, `files.qip`; rename Template→GameTank (the `.qsf` references
   the top-level file and `files.qip`; `gametank-build` expects `GameTank.qpf`).
2. Create `rtl/pll/` (Quartus IP: CLK_50M in → system clock out; video/pixel
   clock-enables are derived, everything runs off 3.579545 MHz enables —
   see HARDWARE.md §Clocks).
3. Vendor CPU (`rtl/cpu/`) and VIA (`rtl/via/`) at the commits pinned in
   DEPENDENCIES.md; keep upstream headers; update DEPENDENCIES.md.
4. Minimal `emu` body: config string, test-pattern video via `ce_pix`
   (steal the pattern generator idea from Template's stock demo), silence on
   audio, no DDR3 yet.
5. `./tools/gametank-build --no-deploy` must produce
   `output_files/GameTank.rbf`; deploy to the DE10 and confirm the test
   pattern + OSD name.
6. Set up `sim/`: thin sim-top instantiating the core only (never `sys/`),
   C++ harness playing the hps_io/ioctl role — pattern reference:
   https://github.com/JimmyStones/Verilator_Template (built on Verilator
   4.204; expect small API fixes on 5.x). Add `sim/Makefile` with `unit` /
   `integration` targets so `gametank-test` and CI go green.
7. Gate: CI green + `.rbf` boots to test pattern on hardware → M1 done, mark
   it in DEVELOPMENT_PLAN.md.

## Reference repos (read, don't copy blindly)

- AtariLynx_MiSTer — closest structural analog (ioctl → ddram.sv cart load)
- nikiiv/Oric_MiSTer — the build-script lineage (`tools/oric-build`)
- clydeshaffer/GameTankEmulator — behavioral reference; lockstep test model
- clydeshaffer/gametank — schematics/BOM (authoritative hardware truth)

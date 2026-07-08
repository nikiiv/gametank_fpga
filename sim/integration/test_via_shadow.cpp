// M8: VIA reads must be emulator-exact. The compatibility floor implements
// the VIA as a bare register file (MemoryReadResolve: reads return
// VIA_regs[addr & 0xF], timers never tick, IRQs never fire). Ganymede
// sweeps all 16 VIA registers as an entropy source at boot and reads them
// ~32k times during level load; live via6522 timer values fork its level
// generator away from the emulator's output.
//
// Cart: writes T1C-L/T1C-H ($2804/$2805), spins ~2000 cycles, reads back
// $2804/$2805 and IFR ($280D) and stores them to RAM. A real 6522 would
// return a decremented counter and a T1-timeout IFR bit; the register
// file must return exactly the written bytes (and IFR = 0).
#include "sim_harness.h"

int main(int, char**) {
    std::vector<uint8_t> img(32768, 0xFF);

    const uint32_t CODE = 0x6000;            // -> $E000
    std::vector<uint8_t> prog = {
        0xA9, 0x77, 0x8D, 0x04, 0x28,        // T1C-L = $77
        0xA9, 0x33, 0x8D, 0x05, 0x28,        // T1C-H = $33 (starts a real T1)
        0xA2, 0x02,                          // LDX #2    (~2.6k cycles)
        0xA0, 0x00,                          // outer: LDY #0
        0x88, 0xD0, 0xFD,                    // inner: DEY; BNE inner
        0xCA, 0xD0, 0xF8,                    // DEX; BNE outer
        0xAD, 0x04, 0x28, 0x85, 0x20,        // $20 = T1C-L readback
        0xAD, 0x05, 0x28, 0x85, 0x21,        // $21 = T1C-H readback
        0xAD, 0x0D, 0x28, 0x85, 0x22,        // $22 = IFR readback
        0xA9, 0x01, 0x85, 0x23,              // $23 = 1 (done)
        0x80, 0xFE,                          // BRA *
    };
    std::copy(prog.begin(), prog.end(), img.begin() + CODE);

    img[0x7FFA] = 0x00; img[0x7FFB] = 0xE0;  // NMI (unused)
    img[0x7FFC] = 0x00; img[0x7FFD] = 0xE0;  // RESET -> $E000
    img[0x7FFE] = 0x00; img[0x7FFF] = 0xE0;  // IRQ (unused)

    Sim sim;
    sim.gtrDownload(img);
    sim.reset();

    uint64_t guard = 4'000'000;
    while (sim.sysram(0x23) != 1 && guard--) sim.tick();
    CHECK(sim.sysram(0x23) == 1, "program completed");

    std::printf("T1C-L=%02x T1C-H=%02x IFR=%02x\n", sim.sysram(0x20),
                sim.sysram(0x21), sim.sysram(0x22));
    CHECK(sim.sysram(0x20) == 0x77,
          "T1C-L reads back the written byte (bare register file)");
    CHECK(sim.sysram(0x21) == 0x33,
          "T1C-H reads back the written byte (bare register file)");
    CHECK(sim.sysram(0x22) == 0x00, "IFR stays clear (no live timer IRQs)");

    std::printf("PASS via_shadow: VIA reads are emulator-exact "
                "(register file, no live timers)\n");
    return 0;
}

// Integration test: the first real test cart (built with cc65) runs on the
// integrated core — CPU boots from the cart's reset vector, executes, and
// leaves a known pattern in the banked system RAM. Asserts physical BRAM
// contents afterwards. See sim/testroms/ram_pattern/ram_pattern.s.

#include "sim_harness.h"

int main() {
    Sim sim;
    sim.loadCart("../testroms/ram_pattern.gtr");
    sim.reset();

    // The cart needs ~5.3k CPU cycles (two 256-byte fills dominate);
    // run 8k CPU cycles = 64k clk.
    for (int i = 0; i < 64000; i++) sim.tick();

    CHECK(sim.sysram(0x1FFF) == 0xC3, "completion marker: %02x",
          sim.sysram(0x1FFF));

    for (int i = 0; i < 256; i++) {
        uint8_t want = (uint8_t)(i ^ 0x5A);
        CHECK(sim.sysram(0x0040 + i) == want, "fill[%02x] = %02x, want %02x",
              i, sim.sysram(0x0040 + i), want);
        CHECK(sim.sysram(0x0140 + i) == want, "fill2[%02x] = %02x, want %02x",
              i, sim.sysram(0x0140 + i), want);
    }

    CHECK(sim.sysram(0x0000) == 0xB0, "bank0 marker: %02x", sim.sysram(0x0000));
    CHECK(sim.sysram(0x2000) == 0xB1, "bank1 marker: %02x", sim.sysram(0x2000));
    CHECK(sim.sysram(0x4000) == 0xB2, "bank2 marker: %02x", sim.sysram(0x4000));
    CHECK(sim.sysram(0x6000) == 0xB3, "bank3 marker: %02x", sim.sysram(0x6000));

    std::printf("PASS cart_ram_pattern: cc65 cart booted, pattern + banks OK\n");
    return 0;
}

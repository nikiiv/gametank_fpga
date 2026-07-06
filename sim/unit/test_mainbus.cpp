// Directed test for our address decode + $2005 RAM banking (rtl/mainbus.sv).
//
// A hand-assembled cart program writes a distinct byte at CPU address $0000
// under each of the four RAM banks, switches back to bank 0 and copies the
// value it reads to another location. The test asserts the four writes
// landed at distinct 8 KB physical offsets in the 32 KB BRAM and that
// readback through the window works.

#include "sim_harness.h"

int main() {
    Sim sim;

    std::vector<uint8_t> prog = {
        0xA9, 0x11, 0x85, 0x00,              // LDA #$11        STA $00   (bank 0)
        0xA9, 0x40, 0x8D, 0x05, 0x20,        // LDA #$40        STA $2005 (bank 1)
        0xA9, 0x22, 0x85, 0x00,              // LDA #$22        STA $00
        0xA9, 0x80, 0x8D, 0x05, 0x20,        // LDA #$80        STA $2005 (bank 2)
        0xA9, 0x33, 0x85, 0x00,              // LDA #$33        STA $00
        0xA9, 0xC0, 0x8D, 0x05, 0x20,        // LDA #$C0        STA $2005 (bank 3)
        0xA9, 0x44, 0x85, 0x00,              // LDA #$44        STA $00
        0xA9, 0x00, 0x8D, 0x05, 0x20,        // LDA #$00        STA $2005 (bank 0)
        0xA5, 0x00, 0x85, 0x02,              // LDA $00         STA $02   (readback)
        0xA9, 0xAA, 0x85, 0x01,              // LDA #$AA        STA $01
    };
    uint16_t trap = (uint16_t)(0x8000 + prog.size());
    prog.push_back(0x4C);                    // JMP self (done marker)
    prog.push_back(trap & 0xFF);
    prog.push_back(trap >> 8);

    std::copy(prog.begin(), prog.end(), sim.cart.begin());
    sim.cart[0x7FFC] = 0x00;                 // reset vector = $8000
    sim.cart[0x7FFD] = 0x80;

    sim.reset();

    if (std::getenv("BUS_TRACE")) {
        for (int i = 0; i < 400; i++) {
            sim.tick();
            std::printf("t%3d cart_addr=%04x data=%02x\n", i,
                        sim.top.cart_addr, sim.top.cart_data);
        }
        return 0;
    }

    for (int i = 0; i < 50000; i++) sim.tick();  // ~780 CPU cycles

    CHECK(sim.sysram(0x0000) == 0x11, "bank0 write: %02x", sim.sysram(0x0000));
    CHECK(sim.sysram(0x2000) == 0x22, "bank1 write: %02x", sim.sysram(0x2000));
    CHECK(sim.sysram(0x4000) == 0x33, "bank2 write: %02x", sim.sysram(0x4000));
    CHECK(sim.sysram(0x6000) == 0x44, "bank3 write: %02x", sim.sysram(0x6000));
    CHECK(sim.sysram(0x0002) == 0x11, "bank0 readback: %02x", sim.sysram(0x0002));
    CHECK(sim.sysram(0x0001) == 0xAA, "final write: %02x", sim.sysram(0x0001));
    CHECK(sim.banking() == 0x00, "banking reg: %02x", sim.banking());

    std::printf("PASS mainbus: 4 banks decoded, readback OK\n");
    return 0;
}

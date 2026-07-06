// Directed test for the VDMA window ($4000-$7FFF -> framebuffer):
// CPU_TO_VRAM (dma[5]) gates the window, banking[3] picks the CPU-visible
// page, and reads come back through the same path. Hand-assembled cart.

#include "sim_harness.h"

int main() {
    Sim sim;

    std::vector<uint8_t> prog = {
        0xA9, 0x20, 0x8D, 0x07, 0x20,        // LDA #$20  STA $2007 (CPU_TO_VRAM)
        0xA9, 0x11, 0x8D, 0x00, 0x40,        // LDA #$11  STA $4000 (page0 0,0)
        0xA9, 0x22, 0x8D, 0x7F, 0x40,        // LDA #$22  STA $407F (127,0)
        0xA9, 0x33, 0x8D, 0x80, 0x40,        // LDA #$33  STA $4080 (0,1)
        0xA9, 0x44, 0x8D, 0xFF, 0x7F,        // LDA #$44  STA $7FFF (127,127)
        0xA9, 0x08, 0x8D, 0x05, 0x20,        // LDA #$08  STA $2005 (VRAM page 1)
        0xA9, 0x55, 0x8D, 0x00, 0x40,        // LDA #$55  STA $4000 (page1 0,0)
        0xAD, 0x00, 0x40, 0x85, 0x10,        // LDA $4000 STA $10   (readback pg1)
        0xA9, 0x00, 0x8D, 0x05, 0x20,        // LDA #$00  STA $2005 (page 0)
        0xAD, 0x7F, 0x40, 0x85, 0x11,        // LDA $407F STA $11   (readback pg0)
        0xA9, 0xC3, 0x85, 0x1F,              // LDA #$C3  STA $1F   (done)
    };
    uint16_t trap = (uint16_t)(0x8000 + prog.size());
    prog.push_back(0x4C);
    prog.push_back(trap & 0xFF);
    prog.push_back(trap >> 8);

    std::copy(prog.begin(), prog.end(), sim.cart.begin());
    sim.cart[0x7FFC] = 0x00;
    sim.cart[0x7FFD] = 0x80;

    sim.reset();
    for (int i = 0; i < 50000; i++) sim.tick();

    CHECK(sim.sysram(0x001F) == 0xC3, "done marker: %02x", sim.sysram(0x001F));

    CHECK(sim.vramRead(0, 0x0000) == 0x11, "pg0 (0,0): %02x", sim.vramRead(0, 0));
    CHECK(sim.vramRead(0, 0x007F) == 0x22, "pg0 (127,0): %02x", sim.vramRead(0, 0x7F));
    CHECK(sim.vramRead(0, 0x0080) == 0x33, "pg0 (0,1): %02x", sim.vramRead(0, 0x80));
    CHECK(sim.vramRead(0, 0x3FFF) == 0x44, "pg0 (127,127): %02x", sim.vramRead(0, 0x3FFF));
    CHECK(sim.vramRead(1, 0x0000) == 0x55, "pg1 (0,0): %02x", sim.vramRead(1, 0));

    CHECK(sim.sysram(0x0010) == 0x55, "CPU readback page1: %02x", sim.sysram(0x0010));
    CHECK(sim.sysram(0x0011) == 0x22, "CPU readback page0: %02x", sim.sysram(0x0011));

    std::printf("PASS vram_window: window gated, paged, readable\n");
    return 0;
}

// The SDK's load_spritesheet selects the CPU GRAM-window quadrant with a
// 1x1 COPY blit and clears TRIGGER on the very next store — no IRQ wait:
//     dma = DMA_ENABLE; GX/GY = quadrant bits; W = H = 1;
//     START = 1; START = 0; dma = 0; ...window writes...
// If the engine hasn't run the blit before the START=0 write lands (the
// first pixel needs a GRAM row fetch from DDR3), the trigger is canceled,
// gram_mid never updates, and the sheet decompresses into the WRONG
// quadrant — sprites stored there are invisible forever (Ganymede's idle
// heroine). This test replicates the idiom byte-for-byte from the banked
// cart window (cart fetch traffic included, like real games).

#include "sim_harness.h"

int main() {
    Sim sim;

    std::vector<uint8_t> img(2 * 1024 * 1024, 0xFF);
    auto put = [&](uint32_t off, std::initializer_list<uint8_t> b) {
        std::copy(b.begin(), b.end(), img.begin() + off);
    };
    const uint32_t BK = 0x1FC000;   // bank_mask powers up at $7F -> last bank

    put(BK + 0x300, {0x78, 0xD8,             // SEI CLD
                0xA2, 0xFF, 0x9A,       // LDX #$FF TXS
                0x9C, 0x05, 0x20,       // STZ $2005
                // ---- SDK load_spritesheet quadrant-select idiom ----
                0xA9, 0x01,             // LDA #DMA_ENABLE
                0x8D, 0x07, 0x20,       // STA $2007
                0x9C, 0x00, 0x40,       // STZ $4000 (VX)
                0x9C, 0x01, 0x40,       // STZ $4001 (VY)
                0xA9, 0x80,             // LDA #$80
                0x8D, 0x02, 0x40,       // STA $4002 (GX: quadrant X=1)
                0x8D, 0x03, 0x40,       // STA $4003 (GY: quadrant Y=1)
                0xA9, 0x01,             // LDA #1
                0x8D, 0x04, 0x40,       // W = 1
                0x8D, 0x05, 0x40,       // H = 1
                0x8D, 0x06, 0x40,       // START = 1
                0x9C, 0x06, 0x40,       // START = 0   <- 4 cycles later
                0x9C, 0x07, 0x20,       // STZ $2007 (window -> GRAM)
                // ---- upload through the window ----
                0xA9, 0xAA,             // LDA #$AA
                0x8D, 0x00, 0x40,       // STA $4000
                0xA9, 0xBB,
                0x8D, 0x01, 0x40,       // STA $4001
                0xA9, 0xCC,
                0x8D, 0xFF, 0x40,       // STA $40FF (end of first row)
                0xA9, 0xC3, 0x85, 0x1F, // done marker
                0x4C, 0x3F, 0x83});     // halt: JMP $833F
    put(BK + 0x000, {0x4C, 0x00, 0x83});          // reset stub: JMP $8300
    put(BK + 0x3FFA, {0x3F, 0x83,                 // NMI -> halt
                      0x00, 0xC0,                 // RESET -> $C000
                      0x3F, 0x83});               // IRQ -> halt

    sim.gtrDownloadSparse(img);
    sim.reset();

    uint64_t guard = sim.cycles + 4000000;
    while (sim.sysram(0x001F) != 0xC3 && sim.cycles < guard) sim.tick();
    CHECK(sim.sysram(0x001F) == 0xC3, "cart did not finish");

    // quadrant (GY7=1, GX7=1) of bank 0 starts at GRAM byte 0xC000
    // (paddr = {bank[2:0], GY7, GX7, gy[6:0], gx[6:0]} -> bits 15|14)
    const uint32_t Q3 = 0xC000;
    CHECK(sim.ddr[Q3 + 0x00] == 0xAA, "quadrant3[0]: %02x want AA (landed in "
          "q0? [0]=%02x)", sim.ddr[Q3 + 0x00], sim.ddr[0]);
    CHECK(sim.ddr[Q3 + 0x01] == 0xBB, "quadrant3[1]: %02x", sim.ddr[Q3 + 1]);
    CHECK(sim.ddr[Q3 + 0xFF] == 0xCC, "quadrant3[255]: %02x", sim.ddr[Q3 + 0xFF]);
    CHECK(sim.ddr[0x00] != 0xAA, "bytes leaked into quadrant 0");

    std::printf("PASS gram_window_quadrant: SDK 1x1 dummy-blit quadrant "
                "select + window upload\n");
    return 0;
}

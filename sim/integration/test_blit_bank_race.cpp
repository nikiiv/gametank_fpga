// Regression test for the Ganymede popping-sprites bug (M8, part 2): the
// SDK's draw-queue helpers rewrite $2005 from the MAINLINE (RAM-bank dance
// for the queue tables: 0 -> $40 -> bank) while IRQ-chained blits are
// still in flight. The emulator is immune — it CatchUp()s the blitter
// before every $2005/$2007 write (gte.cpp) — but a concurrent engine that
// samples banking[2:0] (GRAM bank) and banking[5:4] (clip) live per pixel
// re-sources the rest of the blit: sprite chunks from the wrong GRAM bank
// (often zeros -> transparent -> invisible).
//
// Here: a 16x16 sprite blit from GRAM bank 2 is triggered, and the very
// next mainline instruction writes $2005 = $40 (bank bits -> 0). Emulator
// semantics: the whole sprite still comes from bank 2. Every pixel is
// asserted against the bank-2 pattern.

#include "sim_harness.h"

static uint8_t pat2(int gx, int gy) { return (uint8_t)(0x40 ^ (gx + gy * 5)); }

int main() {
    Sim sim;

    std::vector<uint8_t> img(2 * 1024 * 1024, 0xFF);
    auto put = [&](uint32_t off, std::initializer_list<uint8_t> b) {
        std::copy(b.begin(), b.end(), img.begin() + off);
    };
    const uint32_t BK = 0x1FC000;   // bank_mask powers up at $7F -> last bank

    put(BK + 0x300, {0x78, 0xD8,             // SEI CLD
                0xA2, 0xFF, 0x9A,       // LDX #$FF TXS
                0x64, 0x02,             // STZ $02 (done flag)
                0xA9, 0x02,             // LDA #2
                0x8D, 0x05, 0x20,       // STA $2005 (GRAM bank 2)
                0xA9, 0xC1,             // LDA #$C1: OPAQUE|IRQ|COPY_ENABLE
                0x8D, 0x07, 0x20,       // STA $2007
                0xA9, 0x20,             // LDA #32
                0x8D, 0x00, 0x40,       // VX = 32
                0x8D, 0x01, 0x40,       // VY = 32
                0x9C, 0x02, 0x40,       // GX = 0
                0x9C, 0x03, 0x40,       // GY = 0
                0xA9, 0x10,             // LDA #16
                0x8D, 0x04, 0x40,       // W = 16
                0x8D, 0x05, 0x40,       // H = 16
                0x58,                   // CLI
                0xA9, 0x01,             // LDA #1
                0x8D, 0x06, 0x40,       // START = 1  (256-cycle blit begins)
                0xA9, 0x40,             // LDA #$40
                0x8D, 0x05, 0x20,       // STA $2005  <- mainline banking dance
                                        //    mid-blit (bank bits now 0!)
                0xCB,                   // WAI (blit IRQ)
                0xA5, 0x02,             // LDA $02
                0xF0, 0xFB,             // BEQ -5 (back to WAI)
                0x9C, 0x05, 0x20,       // STZ $2005 (back to RAM bank 0)
                0xA9, 0xC3, 0x85, 0x1F, // done marker
                0x4C, 0x3F, 0x83});     // halt: JMP $833F
    // IRQ: ack trigger, set done
    put(BK + 0x400, {0x48,                   // PHA
                0x9C, 0x06, 0x40,       // STZ $4006 (ack)
                0xA9, 0x01, 0x85, 0x02, // done = 1
                0x68, 0x40});           // PLA RTI
    put(BK + 0x000, {0x4C, 0x00, 0x83});          // reset stub
    put(BK + 0x3FFA, {0x3F, 0x83,                 // NMI -> halt
                      0x00, 0xC0,                 // RESET -> $C000
                      0x00, 0x84});               // IRQ -> $8400

    // GRAM bank 2, quadrant 0: the sprite source. Bank 0 left as zeros so
    // a wrong-bank fetch is visible.
    for (int gy = 0; gy < 16; gy++)
        for (int gx = 0; gx < 16; gx++)
            sim.ddr[0x20000u + (uint32_t)(gy * 128 + gx)] = pat2(gx, gy);

    sim.gtrDownloadSparse(img);
    sim.reset();

    uint64_t guard = sim.cycles + 4000000;
    while (sim.sysram(0x001F) != 0xC3 && sim.cycles < guard) sim.tick();
    CHECK(sim.sysram(0x001F) == 0xC3, "cart did not finish");

    int bad = 0;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            uint8_t got  = sim.vramRead(0, (uint16_t)((32+y)*128 + 32+x));
            uint8_t want = pat2(x, y);
            if (got != want && bad++ < 8)
                std::fprintf(stderr, "px(%d,%d): %02x want %02x\n",
                             x, y, got, want);
        }
    CHECK(bad == 0, "%d pixels re-sourced by the mid-blit banking write", bad);

    std::printf("PASS blit_bank_race: mid-blit $2005 write waits for the "
                "engine (CatchUp semantics)\n");
    return 0;
}

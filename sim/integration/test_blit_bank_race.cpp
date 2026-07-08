// Emulator-fidelity test for mid-blit banking writes: the emulator
// live-samples $2005 — CatchUp() advances the blitter only to CURRENT CPU
// time before the write applies (blitter.cpp:45-49), so pixels blitted
// before the write use the old GRAM bank and pixels after it use the new
// one. (An earlier M8 fix misread CatchUp as running the blit to
// completion and stalled such writes — that DIVERGED from the emulator
// and is reverted; this test locks in the true semantics.)
//
// Here: a 16x16 sprite blit from GRAM bank 2 is triggered, and the very
// next mainline instruction writes $2005 = $40 (bank bits -> 0, whose
// GRAM holds zeros). Expectation: a clean split — an initial run of
// bank-2 pixels (the write lands a few cycles into the blit), then all
// zeros; no interleaving, and the split lands strictly inside the blit.

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

    // power-on bank is now 0 (emulator-matched): mirror the last-bank code
    // into bank 0 so the $8000 window still sees it
    std::copy(img.begin() + BK, img.begin() + BK + 0x4000, img.begin());

    sim.gtrDownloadSparse(img);
    sim.reset();

    uint64_t guard = sim.cycles + 4000000;
    while (sim.sysram(0x001F) != 0xC3 && sim.cycles < guard) sim.tick();
    CHECK(sim.sysram(0x001F) == 0xC3, "cart did not finish");

    // scan in blit order (row-major): must be pat2-prefix then zero-suffix
    int split = -1, bad = 0;
    for (int i = 0; i < 256 && bad == 0; i++) {
        int x = i & 15, y = i >> 4;
        uint8_t got = sim.vramRead(0, (uint16_t)((32+y)*128 + 32+x));
        if (split < 0) {
            if (got == pat2(x, y)) continue;        // still in the old-bank run
            if (got == 0) { split = i; continue; }  // the write landed here
            bad++;
        }
        else if (got != 0) bad++;
        if (bad)
            std::fprintf(stderr, "px %d (%d,%d): %02x (split at %d)\n",
                         i, x, y, got, split);
    }
    CHECK(bad == 0, "pixels interleaved across the banking write");
    CHECK(split > 0, "the $2005 write beat the first pixel (split=%d)", split);
    CHECK(split < 256, "the write never affected the blit");
    std::printf("PASS blit_bank_race: live-sampled mid-blit $2005 write "
                "splits the blit cleanly at pixel %d (emulator semantics)\n",
                split);
    return 0;
}

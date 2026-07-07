// Regression test for the Ganymede sprite bug (M8): sprite blits chained
// from the blit-IRQ handler while the CPU executes from the DDR3-backed
// banked cart window. The cart fetch traffic contends with the GRAM
// prefetcher on the shared DDR port, the pixel engine falls behind the
// exact-duration IRQ counter, and the next TRIGGER (fired from the IRQ
// handler, as the SDK does) used to abandon the tail of the still-draining
// previous sprite — clipped/garbled sprites in-game.
//
// The cart draws a 4x4 grid of 8x8 sprites from GRAM (opaque copy), each
// triggered from the IRQ handler of the previous one. The polling main
// loop and the IRQ handler both live in the banked window to keep cart
// misses flowing during the blits. Every sprite pixel is asserted against
// the seeded GRAM pattern.

#include "sim_harness.h"

static uint8_t pat(int gx, int gy) { return (uint8_t)(0x20 + gx + gy * 3); }

int main() {
    Sim sim;

    std::vector<uint8_t> img(2 * 1024 * 1024, 0xFF);
    auto put = [&](uint32_t off, std::initializer_list<uint8_t> b) {
        std::copy(b.begin(), b.end(), img.begin() + off);
    };

    // ---- banked window ----------------------------------------------------
    // bank_mask powers up as $7F (pull-ups), so without SPI programming the
    // $8000-$BFFF window shows the LAST bank — place the code there
    // (file 0x1FC000+, same bytes the fixed bank serves at $C000+).
    const uint32_t BK = 0x1FC000;
    // sprite table: 16 entries of (VX, VY, GX, GY) at $8100 (data reads
    // from the banked window add further cart traffic)
    // narrow-tall sprites are the worst case: same 64-cycle IRQ duration
    // as 8x8 but twice the row prefetches — the engine lags the furthest
    for (int i = 0; i < 16; i++) {
        int col = i & 3, row = i >> 2;
        img[BK + 0x100 + i * 4 + 0] = (uint8_t)(16 + col * 24);   // VX
        img[BK + 0x100 + i * 4 + 1] = (uint8_t)(4 + row * 28);    // VY
        img[BK + 0x100 + i * 4 + 2] = (uint8_t)(col * 4);         // GX
        img[BK + 0x100 + i * 4 + 3] = (uint8_t)(row * 16);        // GY
    }

    // trigger_next at $8200: X = sprite index (0..15); clobbers A
    //   reads the table, writes params, fires TRIGGER (with IRQ enable)
    put(BK + 0x200, {0x8A,                   // TXA
                0x0A, 0x0A,             // ASL ASL (index*4)
                0xA8,                   // TAY
                0xB9, 0x00, 0x81,       // LDA $8100,Y  (VX)
                0x8D, 0x00, 0x40,       // STA $4000
                0xB9, 0x01, 0x81,       // LDA $8101,Y  (VY)
                0x8D, 0x01, 0x40,
                0xB9, 0x02, 0x81,       // GX
                0x8D, 0x02, 0x40,
                0xB9, 0x03, 0x81,       // GY
                0x8D, 0x03, 0x40,
                0xA9, 0x04,             // LDA #4 (W)
                0x8D, 0x04, 0x40,
                0xA9, 0x10,             // H = 16
                0x8D, 0x05, 0x40,
                0xA9, 0x01,             // TRIGGER
                0x8D, 0x06, 0x40,
                0x60});                 // RTS

    // main at $8300: init, first trigger, then poll zp $02 (done flag)
    // across several 8-byte words to keep instruction fetches missing
    put(BK + 0x300, {0x78, 0xD8,             // SEI CLD
                0xA2, 0xFF, 0x9A,       // LDX #$FF TXS
                0x9C, 0x05, 0x20,       // STZ $2005 (banking)
                0xA9, 0xC1,             // LDA #$C1: OPAQUE|IRQ|COPY_ENABLE
                0x8D, 0x07, 0x20,       // STA $2007
                0x64, 0x02,             // STZ $02 (done)
                0xA2, 0x00,             // LDX #0 (sprite index)
                0x86, 0x03,             // STX $03
                0x58,                   // CLI
                0x20, 0x00, 0x82,       // JSR trigger_next (sprite 0)
                // poll loop, padded with long NOP-ish stretches so the
                // fetch stream spans multiple cart words
                0xA5, 0x02,             // poll: LDA $02
                0xD0, 0x14,             // BNE done (+20)
                0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA,
                0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA,
                0xEA, 0xEA,             // 18 NOPs
                0x80, 0xE8,             // BRA poll (-24)
                0xA9, 0xC3, 0x85, 0x1F, // done: marker $1F = $C3
                0x4C, 0x33, 0x83});     // halt: JMP $8333

    // IRQ handler at $8400: next sprite or set done; RTI
    put(BK + 0x400, {0x48,                   // PHA
                0xE6, 0x03,             // INC $03 (index)
                0xA6, 0x03,             // LDX $03
                0xE0, 0x10,             // CPX #16
                0xB0, 0x06,             // BCS fin (+6)
                0x20, 0x00, 0x82,       // JSR trigger_next
                0x68,                   // PLA
                0x40,                   // RTI
                0xEA,                   // (pad)
                0xA9, 0x01,             // fin: LDA #1
                0x85, 0x02,             // STA $02 (done)
                0xA9, 0x00,             // LDA #0
                0x8D, 0x06, 0x40,       // STA $4006 (TRIGGER=0: ack IRQ)
                0x68,                   // PLA
                0x40});                 // RTI

    // ---- fixed bank: reset stub jumps into the banked window -------------
    put(BK + 0x000, {0x4C, 0x00, 0x83});          // JMP $8300
    put(BK + 0x3FFA, {0x00, 0xC0,                 // NMI (unused)
                      0x00, 0xC0,                 // RESET -> $C000
                      0x00, 0x84});               // IRQ -> $8400 (banked)

    // ---- GRAM: quadrant 0 seeded with the sprite source pattern ----------
    // byte offset within the GRAM region = gy*128 + gx (bank 0, quadrant 0)
    for (int gy = 0; gy < 128; gy++)
        for (int gx = 0; gx < 128; gx++)
            sim.ddr[(uint32_t)(gy * 128 + gx)] = pat(gx, gy);

    // power-on bank is now 0 (emulator-matched): mirror the last-bank code
    // into bank 0 so the $8000 window still sees it
    std::copy(img.begin() + BK, img.begin() + BK + 0x4000, img.begin());

    sim.gtrDownloadSparse(img);
    sim.reset();

    uint64_t guard = sim.cycles + 8000000;
    while (sim.sysram(0x001F) != 0xC3 && sim.cycles < guard) sim.tick();
    if (sim.sysram(0x001F) != 0xC3) {
        std::fprintf(stderr,
                     "debug: dma_ctl=%02x idx=%d done=%d vram(16,16)=%02x "
                     "banking=%02x\n",
                     sim.dmaCtl(), sim.sysram(3), sim.sysram(2),
                     sim.vramRead(0, 16*128+16), sim.banking());
    }
    CHECK(sim.sysram(0x001F) == 0xC3, "cart did not finish (index %d)",
          sim.sysram(0x0003));

    // every pixel of every sprite must match the GRAM source
    int bad = 0, badSprites = 0;
    for (int i = 0; i < 16; i++) {
        int col = i & 3, row = i >> 2;
        int vx0 = 16 + col * 24, vy0 = 4 + row * 28;
        int gx0 = col * 4, gy0 = row * 16;
        int before = bad;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 4; x++) {
                uint8_t got  = sim.vramRead(0, (uint16_t)((vy0+y)*128 + vx0+x));
                uint8_t want = pat(gx0+x, gy0+y);
                if (got != want && bad++ < 8)
                    std::fprintf(stderr,
                                 "sprite %d px(%d,%d): %02x want %02x\n",
                                 i, x, y, got, want);
            }
        badSprites += (bad != before);
    }
    CHECK(bad == 0, "%d wrong pixels across %d sprites", bad, badSprites);

    std::printf("PASS blit_contention: 16 IRQ-chained sprites intact under "
                "cart DDR contention\n");
    return 0;
}

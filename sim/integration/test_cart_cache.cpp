// M8 Ganymede flicker: the banked cart window must serve steady-state
// re-reads with ZERO stalls. Real hardware has no cart latency; the 2-slot
// word buffer thrashed on Ganymede's per-frame sprite/tile reads (~7k
// stall clks per frame), stealing enough CPU time that composition missed
// the flip and an underdrawn page was displayed (visible flicker).
//
// Program: two passes over 1 KB of banked-window data at $8000, stride 1.
// Pass 1 may stall (first touch fills the cache); pass 2 must not stall
// at all. Also touches $C000+ (fixed BRAM, never stalls) in between to
// prove unrelated reads don't evict.
#include "sim_harness.h"

int main(int, char**) {
    std::vector<uint8_t> img(2 * 1024 * 1024, 0xFF);
    // fill bank 0's first 1 KB with a pattern
    for (int i = 0; i < 1024; i++) img[i] = (uint8_t)(i * 7 + 3);

    // program in the fixed bank ($C000+ = image 0x1FC000+):
    //   pass: LDA abs,X over $8000-$83FF, EOR into checksum at $10
    // X loop 0..255 over four pages, two passes, then store checksum to
    // $0011 and spin.
    const uint32_t FIX = 0x1FC000;
    std::vector<uint8_t> prog = {
        0xA9, 0x00, 0x85, 0x10,             // LDA #0; STA $10
        0xA2, 0x00,                         // LDX #0
        // loop1: 4 pages
        0xBD, 0x00, 0x80, 0x45, 0x10, 0x85, 0x10,   // LDA $8000,X EOR $10 STA $10
        0xBD, 0x00, 0x81, 0x45, 0x10, 0x85, 0x10,
        0xBD, 0x00, 0x82, 0x45, 0x10, 0x85, 0x10,
        0xBD, 0x00, 0x83, 0x45, 0x10, 0x85, 0x10,
        0xE8,                               // INX
        0xD0, 0xE1,                         // BNE loop1 (-31)
        0xA9, 0x01, 0x85, 0x12,             // STA $12 <- 1  (marker: pass2 begins)
        0xA2, 0x00,                         // LDX #0
        // loop2
        0xBD, 0x00, 0x80, 0x45, 0x10, 0x85, 0x10,
        0xBD, 0x00, 0x81, 0x45, 0x10, 0x85, 0x10,
        0xBD, 0x00, 0x82, 0x45, 0x10, 0x85, 0x10,
        0xBD, 0x00, 0x83, 0x45, 0x10, 0x85, 0x10,
        0xE8,
        0xD0, 0xE1,                         // BNE loop2 (-31)
        0xA9, 0x02, 0x85, 0x12,             // STA $12 <- 2  (done)
        0x80, 0xFE,                         // BRA *
    };
    std::copy(prog.begin(), prog.end(), img.begin() + FIX);
    // vectors (image end maps to $FFFF)
    img[0x1FFFFA] = 0x00; img[0x1FFFFB] = 0xC0;   // NMI (unused)
    img[0x1FFFFC] = 0x00; img[0x1FFFFD] = 0xC0;   // RESET -> $C000
    img[0x1FFFFE] = 0x00; img[0x1FFFFF] = 0xC0;   // IRQ (unused)

    Sim sim;
    sim.gtrDownloadSparse(img);
    sim.reset();

    auto ram = [&](uint16_t a) { return sim.sysram(a); };

    uint64_t stallP1 = 0, stallP2 = 0, guard = 40'000'000;
    while (ram(0x12) != 2 && guard--) {
        sim.tick();
        bool stall = sim.top.rootp->gametank__DOT__cart_stall;
        if (ram(0x12) == 0) stallP1 += stall;
        else if (ram(0x12) == 1) stallP2 += stall;
    }
    CHECK(ram(0x12) == 2, "program completed");
    std::printf("pass1 stalls=%llu clks, pass2 stalls=%llu clks\n",
                (unsigned long long)stallP1, (unsigned long long)stallP2);
    CHECK(stallP1 > 0, "first touch misses (sanity: DDR is really behind)");
    CHECK(stallP2 == 0, "steady-state re-reads stall zero clocks");
    std::printf("PASS cart_cache: banked window steady-state reads are "
                "stall-free\n");
    return 0;
}

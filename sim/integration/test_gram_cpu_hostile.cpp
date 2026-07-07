// CPU GRAM-window read/write integrity under adversarial DDR timing.
// The SDK builds sprite frame tables by reading sheet headers back from
// GRAM through the $4000 window right after upload (sprites.c) — a flaky
// read there permanently corrupts a frame's draw parameters (position and
// mirror bits): Ganymede's wrong-place/wrong-flip symptom. This cart
// interleaves blits (prefetch traffic in flight) with CPU window reads
// and writes and verifies every byte (the window switch waits for blit
// completion first — dropping COPY_ENABLE mid-blit voids the tail on the
// emulator too). Meant to be run both plain and
// under GT_DDR_HOSTILE=1 (CI runs it plain; the hostile harness models
// the real HPS port's variable latency/busy/beat gaps).

#include "sim_harness.h"

static uint8_t pat(int i) { return (uint8_t)(0x35 ^ (i * 7)); }

int main() {
    Sim sim;

    std::vector<uint8_t> img(2 * 1024 * 1024, 0xFF);
    const uint32_t BK = 0x1FC000;
    static const uint8_t prog[] = { 0x78, 0xD8, 0xA2, 0xFF, 0x9A, 0x9C, 0x05, 0x20, 0xA2, 0x00, 0x86, 0x10, 0xA9, 0x81, 0x8D, 0x07, 0x20, 0xA9, 0x64, 0x8D, 0x00, 0x40, 0x8D, 0x01, 0x40, 0x9C, 0x02, 0x40, 0xA9, 0x40, 0x8D, 0x03, 0x40, 0xA9, 0x10, 0x8D, 0x04, 0x40, 0x8D, 0x05, 0x40, 0xA9, 0x01, 0x8D, 0x06, 0x40, 0xA2, 0x70, 0xCA, 0xD0, 0xFD, 0x9C, 0x07, 0x20, 0xA0, 0x00, 0xB9, 0x00, 0x40, 0x99, 0x00, 0x04, 0xC8, 0xC0, 0x20, 0xD0, 0xF5, 0xA6, 0x10, 0xA5, 0x10, 0x09, 0xA0, 0x9D, 0x00, 0x41, 0xE6, 0x10, 0xA5, 0x10, 0xC9, 0x08, 0xD0, 0xB8, 0xA9, 0xC3, 0x85, 0x1F, 0x4C, 0x58, 0x83 };
    std::copy(prog, prog + sizeof(prog), img.begin() + BK + 0x300);
    img[BK + 0] = 0x4C; img[BK + 1] = 0x00; img[BK + 2] = 0x83;
    img[BK + 0x3FFA] = 0x58; img[BK + 0x3FFB] = 0x83;
    img[BK + 0x3FFC] = 0x00; img[BK + 0x3FFD] = 0xC0;
    img[BK + 0x3FFE] = 0x58; img[BK + 0x3FFF] = 0x83;

    for (int i = 0; i < 128; i++) sim.ddr[i] = pat(i);
    for (int gy = 64; gy < 80; gy++)
        for (int gx = 0; gx < 16; gx++)
            sim.ddr[(uint32_t)(gy * 128 + gx)] = (uint8_t)(gy ^ gx ^ 0x5A);

    // power-on bank is now 0 (emulator-matched): mirror the last-bank code
    // into bank 0 so the $8000 window still sees it
    std::copy(img.begin() + BK, img.begin() + BK + 0x4000, img.begin());

    sim.gtrDownloadSparse(img);
    sim.reset();

    uint64_t guard = sim.cycles + 8000000;
    while (sim.sysram(0x001F) != 0xC3 && sim.cycles < guard) sim.tick();
    CHECK(sim.sysram(0x001F) == 0xC3, "cart did not finish (pass %d)",
          sim.sysram(0x10));

    int bad = 0;
    for (int i = 0; i < 32; i++) {
        uint8_t got = sim.sysram((uint16_t)(0x0400 + i));
        if (got != pat(i) && bad++ < 6)
            std::fprintf(stderr, "read[%d]: %02x want %02x\n", i, got, pat(i));
    }
    CHECK(bad == 0, "%d corrupted CPU GRAM-window reads", bad);

    for (int i = 0; i < 8; i++)
        CHECK(sim.ddr[0x100 + i] == (uint8_t)(0xA0 | i),
              "write[%d]: %02x want %02x", i, sim.ddr[0x100 + i], 0xA0 | i);

    for (int y = 0; y < 16 && bad == 0; y++)
        for (int x = 0; x < 16; x++) {
            uint8_t got  = sim.vramRead(0, (uint16_t)((100+y)*128 + 100+x));
            uint8_t want = (uint8_t)((64+y) ^ x ^ 0x5A);
            if (got != want && bad++ < 6)
                std::fprintf(stderr, "blit(%d,%d): %02x want %02x\n",
                             x, y, got, want);
        }
    CHECK(bad == 0, "blit pixels corrupted by interleaved window traffic");

    std::printf("PASS gram_cpu_hostile: window reads/writes + blits intact\n");
    return 0;
}

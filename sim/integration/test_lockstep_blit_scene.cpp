// Blitter lockstep: blit_scene runs in both the Verilator core and the
// headless emulator. Frames are aligned by the cart's 4-byte tag signature
// at (0..3, 0) — absolute frame indices don't line up across models (59,474
// vs 59,659 cycles/frame), but every *tagged* frame's full 16 KB content
// must match byte-for-byte. Exercises: GRAM quadrant select via dummy blit,
// GRAM loads through the window, colorfill (~COLOR), sprite blits with and
// without transparency, X-mirroring, page flipping, blitter IRQ, vsync NMI.
//
// Usage: test_lockstep_blit_scene <emulator-dump.bin>

#include <map>
#include "sim_harness.h"

static bool tagOf(const uint8_t* fr, uint8_t& tag) {
    uint8_t f = fr[0];
    if (fr[1] != (uint8_t)(f ^ 0xA5) || fr[2] != 0x5A || fr[3] != 0xC3)
        return false;
    tag = f;
    return true;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <emulator-dump.bin>\n", argv[0]);
        return 2;
    }

    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::perror(argv[1]); return 1; }
    std::map<uint8_t, std::array<uint8_t, 16384>> emu;
    std::array<uint8_t, 16384> buf;
    size_t emuFrames = 0;
    while (std::fread(buf.data(), 1, buf.size(), f) == buf.size()) {
        emuFrames++;
        uint8_t tag;
        if (tagOf(buf.data(), tag)) {
            auto it = emu.find(tag);
            if (it != emu.end())
                CHECK(it->second == buf, "emu re-dump of tag %02x differs", tag);
            else
                emu[tag] = buf;
        }
    }
    std::fclose(f);

    Sim sim;
    sim.loadCart("../testroms/blit_scene.gtr");
    sim.reset();
    auto rtlFrames = sim.runFrames((int)emuFrames);

    std::map<uint8_t, std::array<uint8_t, 16384>> rtl;
    for (auto& fr : rtlFrames) {
        uint8_t tag;
        if (tagOf(fr.data(), tag)) {
            auto it = rtl.find(tag);
            if (it != rtl.end())
                CHECK(it->second == fr, "rtl re-dump of tag %02x differs", tag);
            else
                rtl[tag] = fr;
        }
    }

    int common = 0;
    for (auto& [tag, efr] : emu) {
        auto it = rtl.find(tag);
        if (it == rtl.end()) continue;
        common++;
        for (int i = 0; i < 16384; i++) {
            CHECK(it->second[i] == efr[i],
                  "tag %02x diverges at (%d,%d): rtl %02x emu %02x",
                  tag, i & 127, i >> 7, it->second[i], efr[i]);
        }
    }
    CHECK(common >= 8, "only %d common tagged frames (emu %zu, rtl %zu)",
          common, emu.size(), rtl.size());

    std::printf("PASS lockstep_blit_scene: %d tagged frames byte-identical "
                "(emu dumped %zu)\n", common, emuFrames);
    return 0;
}

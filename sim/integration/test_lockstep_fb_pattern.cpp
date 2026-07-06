// First emulator-lockstep test: fb_pattern runs in both the Verilator core
// and the headless-patched GameTankEmulator; per-frame framebuffer dumps
// (16 KB of palette indices at each frame boundary) are compared.
//
// fb_pattern's content is static once its fill completes (~frame 5), and the
// two models boot with different uninitialized VRAM (the emulator randomizes,
// we power up zeroed), so the comparison is over the final frames — the
// steady state both models must agree on byte-for-byte. Dynamic carts tag
// their frames and compare per-tag (see the blit lockstep cart).
//
// Usage: test_lockstep_fb_pattern <emulator-dump.bin>

#include "sim_harness.h"

static uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFF;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & (-(c & 1)));
    }
    return ~c;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <emulator-dump.bin>\n", argv[0]);
        return 2;
    }

    // Emulator side
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::perror(argv[1]); return 1; }
    std::vector<uint8_t> emu;
    uint8_t buf[16384];
    while (std::fread(buf, 1, sizeof buf, f) == sizeof buf)
        emu.insert(emu.end(), buf, buf + sizeof buf);
    std::fclose(f);
    size_t emuFrames = emu.size() / 16384;
    CHECK(emuFrames >= 8, "emulator dump has %zu frames", emuFrames);

    // RTL side
    Sim sim;
    sim.loadCart("../testroms/fb_pattern.gtr");
    sim.reset();
    auto rtl = sim.runFrames((int)emuFrames);

    const uint8_t* emuLast = &emu[(emuFrames - 1) * 16384];
    const uint8_t* rtlLast = rtl.back().data();
    for (int i = 0; i < 16384; i++) {
        CHECK(rtlLast[i] == emuLast[i],
              "steady frame diverges at (%d,%d): rtl %02x emu %02x",
              i & 127, i >> 7, rtlLast[i], emuLast[i]);
    }

    std::printf("PASS lockstep_fb_pattern: %zu frames, steady frame crc %08x "
                "matches emulator\n", emuFrames,
                crc32(rtlLast, 16384));
    return 0;
}

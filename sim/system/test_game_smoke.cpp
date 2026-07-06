// System-tier smoke test: a real SDK-built 2 MB Flash2M game (path in
// argv[1]) is downloaded through the cart port and run for a while; the
// displayed framebuffer must become non-uniform (the game draws its logo).
// Slow (real game boot ≈ dozens of frames) — system tier, not CI.

#include "sim_harness.h"

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s game.gtr\n", argv[0]); return 2; }

    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::perror(argv[1]); return 1; }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::rewind(f);
    std::vector<uint8_t> img((size_t)sz);   // real size — it selects the type
    std::fread(img.data(), 1, img.size(), f);
    std::fclose(f);

    Sim sim;
    sim.gtrDownloadSparse(img);   // full 2 MB byte-path download is slow
    sim.reset();

    // run up to 120 frames; stop when the displayed page shows variety
    for (int frame = 0; frame < 120; frame++) {
        for (int i = 0; i < 476000; i++) sim.tick();
        int page = (sim.dmaCtl() >> 1) & 1;
        std::array<int, 256> hist{};
        for (int i = 0; i < 16384; i++) hist[sim.vramRead(page, (uint16_t)i)]++;
        // drawn = the frame has a dominant background plus a secondary
        // color with real coverage (the SDK logo screen is 2-color)
        int first = 0, second = 0;
        for (int c : hist) {
            if (c > first) { second = first; first = c; }
            else if (c > second) second = c;
        }
        if (frame % 10 == 0)
            std::printf("frame %d: top colors %d / %d px\n", frame, first, second);
        if (second >= 200) {
            std::printf("PASS game_smoke: real Flash2M game draws (frame %d, %d px foreground)\n",
                        frame, second);
            return 0;
        }
    }
    std::fprintf(stderr, "FAIL: framebuffer never showed foreground pixels\n");
    return 1;
}

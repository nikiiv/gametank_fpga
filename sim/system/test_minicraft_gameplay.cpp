// Minicraft gameplay regression harness. Boot the real cartridge, choose
// New World -> Built in with A, then keep the RTL running beyond the point
// where the old hardware build corrupted scanout and appeared to lock up.
//
// An optional output prefix writes staged true-scanout frames. The diagnostic
// counters make a gameplay failure distinguishable from an HDMI/capture fault.

#include "sim_harness.h"

#include <array>
#include <set>

static constexpr int CLKS_PER_FRAME = 1816 * 262;

struct Counters {
    uint64_t instructions = 0;
    uint64_t cpuWrites = 0;
    uint64_t blitWrites = 0;
    uint64_t requestedFlips = 0;
    uint64_t scanoutHandoffs = 0;
    uint64_t visibleHandoffs = 0;
    std::set<uint16_t> pcs;
};

static void runFrames(Sim& sim, int frames, Counters& counts) {
    auto* root = sim.top.rootp;
    uint8_t requestedPage = (sim.dmaCtl() >> 1) & 1;
    uint8_t scanoutPage = root->gametank__DOT__vid_page;

    for (int frame = 0; frame < frames; ++frame) {
        for (int cycle = 0; cycle < CLKS_PER_FRAME; ++cycle) {
            sim.tick();
            if (root->gametank__DOT__cpu_ce) {
                counts.instructions += root->gametank__DOT__mainbus__DOT__cpu_sync;
                if (root->gametank__DOT__mainbus__DOT__cpu_sync)
                    counts.pcs.insert(root->gametank__DOT__mainbus__DOT__cpu_ab);
            }
            counts.cpuWrites += root->gametank__DOT__cpu_vram_we;
            counts.blitWrites += root->gametank__DOT__blit_vram_we;

            const uint8_t nextRequested = (sim.dmaCtl() >> 1) & 1;
            if (nextRequested != requestedPage) {
                requestedPage = nextRequested;
                counts.requestedFlips++;
            }
            const uint8_t nextScanout = root->gametank__DOT__vid_page;
            if (nextScanout != scanoutPage) {
                scanoutPage = nextScanout;
                counts.scanoutHandoffs++;
                if (!sim.top.vblank && !sim.top.hblank)
                    counts.visibleHandoffs++;
            }
        }
    }
}

static void pressA(Sim& sim, Counters& counts) {
    sim.top.joy1 = 1 << 4;
    runFrames(sim, 8, counts);
    sim.top.joy1 = 0;
    runFrames(sim, 24, counts);
}

static uint64_t pageHash(const Sim& sim, int page) {
    uint64_t hash = 14695981039346656037ull;
    for (int i = 0; i < 16384; ++i) {
        hash ^= sim.vramRead(page, (uint16_t)i);
        hash *= 1099511628211ull;
    }
    return hash;
}

static void snapshot(Sim& sim, const char* prefix, const char* stage,
                     const Counters& counts) {
    auto* root = sim.top.rootp;
    std::printf("stage=%s cycles=%llu insn=%llu pcs=%zu dma=%02x bank=%02x "
                "scan=%u req_flips=%llu handoffs=%llu visible=%llu "
                "cpu_wr=%llu blit_wr=%llu "
                "page0=%016llx page1=%016llx\n",
                stage,
                (unsigned long long)sim.cycles,
                (unsigned long long)counts.instructions,
                counts.pcs.size(), sim.dmaCtl(), sim.banking(),
                root->gametank__DOT__vid_page,
                (unsigned long long)counts.requestedFlips,
                (unsigned long long)counts.scanoutHandoffs,
                (unsigned long long)counts.visibleHandoffs,
                (unsigned long long)counts.cpuWrites,
                (unsigned long long)counts.blitWrites,
                (unsigned long long)pageHash(sim, 0),
                (unsigned long long)pageHash(sim, 1));

    if (prefix) {
        Frame frame = captureFrame(sim);
        char path[512];
        std::snprintf(path, sizeof path, "%s_%s.ppm", prefix, stage);
        frame.writePPM(path);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s Minicraft.gtr [out_prefix]\n", argv[0]);
        return 2;
    }

    FILE* file = std::fopen(argv[1], "rb");
    if (!file) {
        std::perror(argv[1]);
        return 1;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::rewind(file);
    std::vector<uint8_t> image((size_t)size);
    CHECK(std::fread(image.data(), 1, image.size(), file) == image.size(),
          "short cartridge read");
    std::fclose(file);

    Sim sim;
    sim.gtrDownloadSparse(image);
    sim.reset();

    Counters counts;
    const char* prefix = argc > 2 ? argv[2] : nullptr;

    runFrames(sim, 300, counts);
    snapshot(sim, prefix, "title", counts);

    pressA(sim, counts);              // New World
    snapshot(sim, prefix, "world_menu", counts);

    pressA(sim, counts);              // Built in
    snapshot(sim, prefix, "loading", counts);

    const std::array<int, 5> waits = {60, 120, 180, 240, 300};
    int elapsed = 0;
    for (int wait : waits) {
        runFrames(sim, wait, counts);
        elapsed += wait;
        char stage[32];
        std::snprintf(stage, sizeof stage, "game_%04d", elapsed);
        snapshot(sim, prefix, stage, counts);

        // The built-in world first shows a green staging frame. By frame 180
        // terrain composition must have begun: the framebuffer content has
        // left the known staging-page hash, page flips have resumed, and the
        // blitter has produced substantially more than the title/menu workload.
        // This prevents a stable but stuck staging frame from satisfying the
        // generic CPU-progress check below. Both pages may legitimately match
        // once the completed terrain has been copied into each backbuffer.
        if (elapsed == 180) {
            constexpr uint64_t stagingPageHash = 0x0a3c190ad959cc86ull;
            const uint64_t page0 = pageHash(sim, 0);
            const uint64_t page1 = pageHash(sim, 1);
            CHECK(page0 != stagingPageHash || page1 != stagingPageHash,
                  "built-in world remained on the green staging frame");
            CHECK(counts.requestedFlips > 150,
                  "built-in world did not resume page flips: %llu",
                  (unsigned long long)counts.requestedFlips);
            CHECK(counts.blitWrites > 3'000'000,
                  "built-in terrain composition did not start: %llu writes",
                  (unsigned long long)counts.blitWrites);
        }
    }

    CHECK(counts.instructions > 1'000'000,
          "CPU made too little progress: %llu instructions",
          (unsigned long long)counts.instructions);
    CHECK(counts.visibleHandoffs == 0,
          "scanout changed framebuffer page %llu times during visible pixels",
          (unsigned long long)counts.visibleHandoffs);
    std::printf("PASS minicraft_gameplay diagnostic run completed\n");
    return 0;
}

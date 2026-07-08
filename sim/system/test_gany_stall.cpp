// Scratch harness for the M8 Ganymede hunt: navigate the menu (Down,
// Start), let the Climb Race load, then dump the static ready-scene as
// PPM frames for eyeball/diff comparison with the emulator's scene.
//
// usage: test_gany_nav game.gtr [out_prefix]
#include "sim_harness.h"
#include <array>

struct Ev { long flip; uint8_t joy; };
// joy bits: 0=R 1=L 2=D 3=U 4=A 5=B 6=C 7=Start
static const Ev SCHED[] = {
    {250, 0x04}, {258, 0x00},        // Down (menu -> Climb Race)
    {270, 0x80}, {278, 0x00},        // Start (confirm)
};
static const int NSCHED = sizeof(SCHED) / sizeof(SCHED[0]);
static const int SETTLE_FLIPS = 800;  // well past load into the ready scene
static const int DUMP_FRAMES  = 12;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s game.gtr [prefix]\n", argv[0]); return 2; }
    const char* prefix = argc > 2 ? argv[2] : "gany_nav";

    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::perror(argv[1]); return 1; }
    std::vector<uint8_t> img(2 * 1024 * 1024, 0xFF);
    std::fread(img.data(), 1, img.size(), f);
    std::fclose(f);

    Sim sim;
    sim.gtrDownloadSparse(img);
    sim.reset();

    long flips = 0;
    uint64_t nStarve = 0, nGram = 0, nCart = 0, lastCyc = sim.cycles;
    int schedPos = 0, dumped = 0;
    uint8_t prevPage = sim.dmaCtl() & 0x02;
    uint64_t guard = sim.cycles + (uint64_t)3000 * 3 * 476000ull;

    while (dumped < DUMP_FRAMES && sim.cycles < guard) {
        sim.tick();
        {
            auto* r = sim.top.rootp;
            nStarve += r->gametank__DOT__blit_starve;
            nGram   += r->gametank__DOT__gram_stall;
            nCart   += r->gametank__DOT__cart_stall;
        }
        uint8_t page = sim.dmaCtl() & 0x02;
        if (page == prevPage) continue;
        prevPage = page;
        if (flips < SETTLE_FLIPS) { nStarve = nGram = nCart = 0; lastCyc = sim.cycles; }
        while (schedPos < NSCHED && SCHED[schedPos].flip <= flips) {
            sim.top.joy1 = SCHED[schedPos].joy;
            schedPos++;
        }
        if (flips >= SETTLE_FLIPS) {
            std::printf("flip %ld: starve=%llu gram=%llu cart=%llu of %llu clks\n",
                        flips, (unsigned long long)nStarve, (unsigned long long)nGram,
                        (unsigned long long)nCart, (unsigned long long)(sim.cycles - lastCyc));

            int p = (sim.dmaCtl() >> 1) & 1;
            char name[128];
            std::snprintf(name, sizeof name, "%s_%03ld.ppm", prefix, flips);
            FILE* o = std::fopen(name, "wb");
            std::fprintf(o, "P5\n128 128\n255\n");
            for (int i = 0; i < 16384; i++)
                std::fputc(sim.vramRead(p, (uint16_t)i), o);
            std::fclose(o);
            dumped++;
            nStarve = nGram = nCart = 0; lastCyc = sim.cycles;
        }
        flips++;
    }
    std::printf("flips=%ld dumped=%d\n", flips, dumped);
    return dumped == DUMP_FRAMES ? 0 : 1;
}

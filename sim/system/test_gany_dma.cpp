// Scratch: trace every $2007 (dma_ctl) change + vsync NMI edges during
// Ganymede's ready scene, to reconstruct the page-flip choreography.
#include "sim_harness.h"

struct Ev { long flip; uint8_t joy; };
static const Ev SCHED[] = {
    {250, 0x04}, {258, 0x00},
    {270, 0x80}, {278, 0x00},
};
static const int NSCHED = sizeof(SCHED) / sizeof(SCHED[0]);

int main(int argc, char** argv) {
    FILE* f = std::fopen(argv[1], "rb");
    std::vector<uint8_t> img(2 * 1024 * 1024, 0xFF);
    std::fread(img.data(), 1, img.size(), f);
    std::fclose(f);

    Sim sim;
    sim.gtrDownloadSparse(img);
    sim.reset();

    long flips = 0;
    int schedPos = 0;
    uint8_t prevPage = sim.dmaCtl() & 0x02;
    uint8_t prevCtl = sim.dmaCtl();
    bool prevNmi = false;
    uint64_t t0 = 0;
    auto* r = sim.top.rootp;

    while (flips < 806) {
        sim.tick();
        if (flips >= 800) {
            uint8_t ctl = sim.dmaCtl();
            if (ctl != prevCtl) {
                std::printf("%9llu ctl %02x -> %02x%s\n",
                            (unsigned long long)(sim.cycles - t0), prevCtl, ctl,
                            ((ctl ^ prevCtl) & 0x02) ? "  <FLIP>" : "");
            }
            bool nmi = r->gametank__DOT__vsync_nmi;
            if (nmi && !prevNmi)
                std::printf("%9llu NMI\n", (unsigned long long)(sim.cycles - t0));
            prevNmi = nmi;
            prevCtl = ctl;
        }
        uint8_t page = sim.dmaCtl() & 0x02;
        if (page == prevPage) continue;
        prevPage = page;
        while (schedPos < NSCHED && SCHED[schedPos].flip <= flips) {
            sim.top.joy1 = SCHED[schedPos].joy;
            schedPos++;
        }
        flips++;
        if (flips == 800) { t0 = sim.cycles; prevCtl = sim.dmaCtl(); }
    }
    return 0;
}

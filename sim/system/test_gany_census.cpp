// Scratch harness for the M8 Ganymede hunt: census of every CPU read
// from a hardware-state source (pads, VIA, ARAM audio window, open bus)
// between pressing Start and the painted ready scene. Any state the
// level generator can consume must pass through one of these.
//
// usage: test_gany_census game.gtr
#include "sim_harness.h"
#include <map>

struct Ev { long flip; uint8_t joy; };
static const Ev SCHED[] = {
    {250, 0x04}, {258, 0x00},        // Down (menu -> Climb Race)
    {270, 0x80}, {278, 0x00},        // Start (confirm)
};
static const int NSCHED = sizeof(SCHED) / sizeof(SCHED[0]);

enum { SEL_RAM, SEL_CART, SEL_VRAM, SEL_GRAM, SEL_PAD, SEL_VIA, SEL_ARAM, SEL_OPEN };

int main(int argc, char** argv) {
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::perror(argv[1]); return 1; }
    std::vector<uint8_t> img(2 * 1024 * 1024, 0xFF);
    std::fread(img.data(), 1, img.size(), f);
    std::fclose(f);

    Sim sim;
    sim.gtrDownloadSparse(img);
    sim.reset();

    long flips = 0;
    int schedPos = 0;
    uint8_t prevPage = sim.dmaCtl() & 0x02;
    uint64_t guard = sim.cycles + (uint64_t)3000 * 3 * 476000ull;

    uint64_t nPad = 0, nVia = 0, nAram = 0, nOpen = 0;
    std::map<unsigned, uint64_t> aramAddr, openAddr;
    bool census = false;
    uint8_t prevSel = 0;

    auto* r = sim.top.rootp;
    while (flips < 810 && sim.cycles < guard) {
        sim.tick();
        if (census && r->gametank__DOT__cpu_ce) {
            uint8_t sel = r->gametank__DOT__mainbus__DOT__rd_sel;
            unsigned wa = r->gametank__DOT__win_addr;
            switch (sel) {
                case SEL_PAD:  nPad++; break;
                case SEL_VIA:  nVia++; break;
                case SEL_ARAM: nAram++; aramAddr[wa & 0x0FFF]++; break;
                case SEL_OPEN: nOpen++; openAddr[wa]++; break;
            }
            prevSel = sel;
        }
        uint8_t page = sim.dmaCtl() & 0x02;
        if (page == prevPage) continue;
        prevPage = page;
        while (schedPos < NSCHED && SCHED[schedPos].flip <= flips) {
            sim.top.joy1 = SCHED[schedPos].joy;
            schedPos++;
        }
        if (flips == 270) census = true;   // Start pressed
        flips++;
    }
    (void)prevSel;
    std::printf("census flips 270-810: pad=%llu via=%llu aram=%llu open=%llu\n",
                (unsigned long long)nPad, (unsigned long long)nVia,
                (unsigned long long)nAram, (unsigned long long)nOpen);
    std::printf("ARAM read addrs (top 24):\n");
    { int i = 0; for (auto& kv : aramAddr) { if (i++ < 24)
        std::printf("  $3%03X x%llu\n", kv.first, (unsigned long long)kv.second); } }
    std::printf("OPEN read addrs (top 24):\n");
    { int i = 0; for (auto& kv : openAddr) { if (i++ < 24)
        std::printf("  $%04X x%llu\n", kv.first, (unsigned long long)kv.second); } }
    return 0;
}

// scratch: does Ganymede read the VIA during boot/load? which registers?
#include "sim_harness.h"

int main(int argc, char** argv) {
    FILE* f = std::fopen(argv[1], "rb");
    std::vector<uint8_t> img(2 * 1024 * 1024);
    std::fread(img.data(), 1, img.size(), f);
    std::fclose(f);

    Sim sim;
    sim.gtrDownloadSparse(img);
    sim.reset();

    auto frames = [&](int n) { for (int i = 0; i < n * 476000; i++) sim.tick(); };
    auto tap = [&](int bit) {
        sim.top.joy1 = (uint8_t)(1 << bit); frames(4);
        sim.top.joy1 = 0; frames(8);
    };

    uint64_t rdCount[16] = {0};
    bool prevRen = false;
    auto watch = [&]() {
        auto* r = sim.top.rootp;
        bool ren = r->gametank__DOT__via_ren;
        if (ren && !prevRen)
            rdCount[r->gametank__DOT__win_addr & 15]++;
        prevRen = ren;
    };
    auto framesW = [&](int n) {
        for (int i = 0; i < n * 476000; i++) { sim.tick(); watch(); }
    };

    framesW(260);
    std::printf("boot+menu VIA reads by reg:");
    for (int i = 0; i < 16; i++) if (rdCount[i]) std::printf(" [%X]=%llu", i, (unsigned long long)rdCount[i]);
    std::printf("\n");
    for (int i = 0; i < 16; i++) rdCount[i] = 0;
    tap(2); tap(7);
    framesW(420);
    std::printf("race-load VIA reads by reg:");
    for (int i = 0; i < 16; i++) if (rdCount[i]) std::printf(" [%X]=%llu", i, (unsigned long long)rdCount[i]);
    std::printf("\n");
    return 0;
}

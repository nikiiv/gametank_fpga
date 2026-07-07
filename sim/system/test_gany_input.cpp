// scratch: find an input pattern that gets Ganymede past its menu
#include "sim_harness.h"

int main(int argc, char** argv) {
    FILE* f = std::fopen(argv[1], "rb");
    std::vector<uint8_t> img(2 * 1024 * 1024);
    std::fread(img.data(), 1, img.size(), f);
    std::fclose(f);

    Sim sim;
    // emulator-style power-on randomization (deterministic seed): games can
    // mis-seed state from all-zero RAM (the emulator randomizes for this)
    uint32_t rng = 0x12345678;
    auto rnd = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (uint8_t)rng; };
    for (int i = 0; i < 0x8000; i++)
        sim.top.rootp->gametank__DOT__mainbus__DOT__sysram[i] = rnd();
    sim.gtrDownloadSparse(img);
    sim.reset();

    // settle on the menu
    for (int frame = 0; frame < 260; frame++)
        for (int i = 0; i < 476000; i++) sim.tick();

    // tap different buttons with press/release edges; watch for scene change
    struct Try { int bit; const char* name; } tries[] = {
        {4, "A"}, {7, "Start"}, {5, "B"}, {6, "C"}, {0, "Right"},
    };
    auto frameSig = [&]() {
        int page = (sim.dmaCtl() >> 1) & 1;
        uint32_t h = 2166136261u;
        for (int i = 0; i < 16384; i += 7)
            h = (h ^ sim.vramRead(page, (uint16_t)i)) * 16777619u;
        return h;
    };
    uint32_t menuSig = frameSig();
    for (auto& t : tries) {
        for (int tap = 0; tap < 3; tap++) {
            sim.top.joy1 = (uint8_t)(1 << t.bit);
            for (int fN = 0; fN < 4; fN++)
                for (int i = 0; i < 476000; i++) sim.tick();
            sim.top.joy1 = 0;
            for (int fN = 0; fN < 8; fN++)
                for (int i = 0; i < 476000; i++) sim.tick();
        }
        uint32_t sig = frameSig();
        std::printf("%s: sig %08x %s\n", t.name, sig,
                    sig != menuSig ? "<-- SCENE CHANGED" : "(menu)");
        if (sig != menuSig) return 0;
    }
    return 1;
}

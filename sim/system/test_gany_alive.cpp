// scratch: is Ganymede's main loop alive? hash sysram + zp over time
#include "sim_harness.h"

int main(int argc, char** argv) {
    FILE* f = std::fopen(argv[1], "rb");
    std::vector<uint8_t> img(2 * 1024 * 1024);
    std::fread(img.data(), 1, img.size(), f);
    std::fclose(f);

    Sim sim;
    sim.gtrDownloadSparse(img);
    sim.reset();

    auto ramHash = [&]() {
        uint32_t h = 2166136261u;
        for (int i = 0; i < 0x2000; i++)
            h = (h ^ sim.sysram((uint16_t)i)) * 16777619u;
        return h;
    };
    for (int s = 0; s < 8; s++) {
        for (int frame = 0; frame < 60; frame++)
            for (int i = 0; i < 476000; i++) sim.tick();
        std::printf("t=%ds ram=%08x zp0-15:", s, ramHash());
        for (int i = 0; i < 16; i++) std::printf(" %02x", sim.sysram(i));
        std::printf("\n");
    }
    return 0;
}

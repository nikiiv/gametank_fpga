// scratch: run Ganymede via the DDR3 cart, dump displayed VRAM per frame
#include "sim_harness.h"

int main(int argc, char** argv) {
    FILE* f = std::fopen(argv[1], "rb");
    std::vector<uint8_t> img(2 * 1024 * 1024);
    std::fread(img.data(), 1, img.size(), f);
    std::fclose(f);

    Sim sim;
    sim.gtrDownloadSparse(img);
    sim.reset();

    FILE* out = std::fopen("gany_frames.bin", "wb");
    for (int frame = 0; frame < 600; frame++) {
        // try Start then A on the menu (held ~20 frames each)
        uint8_t j = 0;
        if (frame >= 280 && frame < 300) j = 1 << 7;   // Start
        else if (frame >= 360 && frame < 380) j = 1 << 4;  // A
        sim.top.joy1 = j;
        for (int i = 0; i < 476000; i++) sim.tick();
        int page = (sim.dmaCtl() >> 1) & 1;
        std::array<uint8_t, 16384> fb;
        for (int i = 0; i < 16384; i++) fb[i] = sim.vramRead(page, (uint16_t)i);
        std::fwrite(fb.data(), 1, fb.size(), out);
    }
    std::fclose(out);
    std::printf("600 frames dumped\n");
    return 0;
}

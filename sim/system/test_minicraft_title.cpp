// System-tier Minicraft regression. Boot the real 2 MiB cartridge through
// the DDR path, settle on the title screen, then require true scanout frames
// to remain pixel-identical. An optional prefix writes the captured frames.

#include "sim_harness.h"

static uint64_t frameHash(const Frame& frame) {
    uint64_t hash = 14695981039346656037ull;
    for (uint8_t byte : frame.rgb) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
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

    // The pinned emulator reaches its stable title at frame 203. DDR-backed
    // RTL boot is slower, so leave a conservative margin before measuring.
    for (int frame = 0; frame < 300; ++frame)
        for (int cycle = 0; cycle < 1816 * 262; ++cycle)
            sim.tick();

    constexpr int framesToCheck = 12;
    uint64_t expectedHash = 0;
    for (int i = 0; i < framesToCheck; ++i) {
        Frame frame = captureFrame(sim);
        CHECK(frame.width == 748 && frame.height == 246,
              "unexpected scanout dimensions %dx%d", frame.width, frame.height);

        const uint64_t hash = frameHash(frame);
        if (i == 0)
            expectedHash = hash;
        CHECK(hash == expectedHash,
              "title frame %d flickered: %016llx != %016llx",
              i, (unsigned long long)hash, (unsigned long long)expectedHash);

        if (argc > 2) {
            char path[512];
            std::snprintf(path, sizeof path, "%s_%02d.ppm", argv[2], i);
            frame.writePPM(path);
        }
    }

    std::printf("PASS minicraft_title: %d stable scanout frames (%016llx)\n",
                framesToCheck, (unsigned long long)expectedHash);
    return 0;
}

// Gameplay lockstep: run Ganymede with a video-frame-indexed scripted input
// schedule and compare displayed frames against the emulator's dump of the
// same schedule. Both sides boot from zeroed memory (GTE_ZERO_POWERON on the
// emulator side). Frames are captured at the vsync-NMI instant, matching the
// patched emulator's dump boundary.
//
// usage: test_gany_lockstep game.gtr emu_frames.bin [--print-input]
//   --print-input prints the GTE_LOCKSTEP_INPUT string and exits.

#include "sim_harness.h"

struct Ev { long frame; uint8_t joy; };
// joy bits: 0=R 1=L 2=D 3=U 4=A 5=B 6=C 7=Start
static const Ev SCHED[] = {
    {600, 0x04}, {608, 0x00},        // wait 10s, Down (menu -> Climb Race)
    {620, 0x80}, {628, 0x00},        // Start/select (confirm)
};
static const int NSCHED = sizeof(SCHED) / sizeof(SCHED[0]);
static const int SAMPLE_START = 1200; // wait another 10s after selection
static const int NFRAMES = 1320;

static unsigned emuMask(uint8_t joy) {
    unsigned m = 0;
    if (joy & 0x01) m |= 0x0100; // Right
    if (joy & 0x02) m |= 0x0200; // Left
    if (joy & 0x04) m |= 0x0404; // Down
    if (joy & 0x08) m |= 0x0808; // Up
    if (joy & 0x10) m |= 0x0010; // A
    if (joy & 0x20) m |= 0x1000; // B
    if (joy & 0x40) m |= 0x2000; // C
    if (joy & 0x80) m |= 0x0020; // Start
    return m;
}

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[argc-1]) == "--print-input") {
        for (int i = 0; i < NSCHED; i++)
            std::printf("%s%ld:%x", i ? "," : "", SCHED[i].frame,
                        emuMask(SCHED[i].joy));
        std::printf("\n");
        return 0;
    }
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s game.gtr emu_frames.bin\n", argv[0]);
        return 2;
    }

    std::vector<uint8_t> img(2 * 1024 * 1024, 0xFF);
    if (FILE* f = std::fopen(argv[1], "rb")) {
        size_t n = std::fread(img.data(), 1, img.size(), f);
        std::fclose(f);
        if (n == 0) {
            std::fprintf(stderr, "empty game image: %s\n", argv[1]);
            return 1;
        }
    } else {
        std::perror(argv[1]);
        return 1;
    }

    std::vector<uint8_t> emu;
    if (FILE* f = std::fopen(argv[2], "rb")) {
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        emu.resize((size_t)sz);
        if (sz > 0) {
            size_t n = std::fread(emu.data(), 1, emu.size(), f);
            if (n != emu.size()) {
                std::fprintf(stderr, "short emulator frame read: %s\n", argv[2]);
                std::fclose(f);
                return 1;
            }
        }
        std::fclose(f);
    } else {
        std::perror(argv[2]);
        return 1;
    }
    const size_t emuN = emu.size() / 16384;

    Sim sim;
    sim.gtrDownloadSparse(img);
    sim.reset();

    int nframes = std::getenv("GT_NFRAMES")
        ? std::atoi(std::getenv("GT_NFRAMES")) : NFRAMES;
    if (nframes <= 0 || nframes > NFRAMES) nframes = NFRAMES;

    FILE* ours = std::fopen("gany_ls_ours.bin", "wb");
    if (!ours) {
        std::perror("gany_ls_ours.bin");
        return 1;
    }

    long frames = 0;
    int schedPos = 0, firstBad = -1, badN = 0;
    int sampleFirstBad = -1, sampleBadN = 0, sampleN = 0;
    bool prevNmi = true;  // the reset-time pulse does not count as a boundary
    uint64_t guard = sim.cycles + (uint64_t)(nframes + 2) * 1816 * 262;

    while (frames < (long)emuN && frames < nframes && sim.cycles < guard) {
        sim.tick();

        bool nmi = sim.top.rootp->gametank__DOT__vsync_nmi;
        if (!nmi || prevNmi || sim.cycles <= 100) {
            prevNmi = nmi;
            continue;
        }
        prevNmi = nmi;

        // Apply schedule entries due at this video frame, matching the
        // emulator lockstep convention.
        while (schedPos < NSCHED && SCHED[schedPos].frame <= frames) {
            sim.top.joy1 = SCHED[schedPos].joy;
            schedPos++;
        }

        uint8_t p = (sim.dmaCtl() & 0x02) ? 1 : 0;
        std::array<uint8_t, 16384> fb{};
        for (int i = 0; i < 16384; i++)
            fb[i] = sim.vramRead(p, (uint16_t)i);
        std::fwrite(fb.data(), 1, fb.size(), ours);

        if (std::memcmp(fb.data(), emu.data() + (size_t)frames * 16384,
                        16384) != 0) {
            if (firstBad < 0) firstBad = (int)frames;
            badN++;
            if (frames >= SAMPLE_START) {
                if (sampleFirstBad < 0) sampleFirstBad = (int)frames;
                sampleBadN++;
            }
        }
        if (frames >= SAMPLE_START) sampleN++;
        frames++;
    }
    std::fclose(ours);

    std::printf("frames=%ld matched=%ld divergent=%d firstBad=%d "
                "sampleFrames=%d sampleDivergent=%d sampleFirstBad=%d\n",
                frames, frames - badN, badN, firstBad, sampleN, sampleBadN,
                sampleFirstBad);
    return sampleBadN ? 1 : 0;
}

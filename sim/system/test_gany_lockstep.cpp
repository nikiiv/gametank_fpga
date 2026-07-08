// Gameplay lockstep: run a real game with a flip-indexed scripted input
// schedule and compare every displayed frame (at page-flip boundaries)
// against the emulator's dump of the same schedule. Both sides boot from
// zeroed memory (GTE_ZERO_POWERON on the emulator side). Frames are
// captured when dma_ctl[1] changes — the same instant the patched
// emulator dumps — so index k compares like for like.
//
// usage: test_gany_lockstep game.gtr emu_frames.bin [--print-input]
//   --print-input prints the GTE_LOCKSTEP_INPUT string and exits.

#include "sim_harness.h"

struct Ev { long flip; uint8_t joy; };
// joy bits: 0=R 1=L 2=D 3=U 4=A 5=B 6=C 7=Start
static const Ev SCHED[] = {
    {250, 0x04}, {258, 0x00},        // Down (menu -> Climb Race)
    {270, 0x80}, {278, 0x00},        // Start (confirm)
    {600, 0x01}, {640, 0x02},        // gameplay: right, left
    {680, 0x11}, {700, 0x01},        // right+A, right
    {740, 0x02}, {760, 0x12},        // left, left+A
    {800, 0x08}, {830, 0x18},        // up, up+A
    {860, 0x00},
};
static const int NSCHED = sizeof(SCHED) / sizeof(SCHED[0]);
static const int NFRAMES = 900;

static unsigned emuMask(uint8_t joy) {
    unsigned m = 0;
    if (joy & 0x01) m |= 0x0100;   // RIGHT
    if (joy & 0x02) m |= 0x0200;   // LEFT
    if (joy & 0x04) m |= 0x0404;   // DOWN
    if (joy & 0x08) m |= 0x0808;   // UP
    if (joy & 0x10) m |= 0x0010;   // A
    if (joy & 0x20) m |= 0x1000;   // B
    if (joy & 0x40) m |= 0x2000;   // C
    if (joy & 0x80) m |= 0x0020;   // START
    return m;
}

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[argc-1]) == "--print-input") {
        for (int i = 0; i < NSCHED; i++)
            std::printf("%s%ld:%x", i ? "," : "", SCHED[i].flip,
                        emuMask(SCHED[i].joy));
        std::printf("\n");
        return 0;
    }
    if (argc < 3) { std::fprintf(stderr, "usage: %s game.gtr emu.bin\n", argv[0]); return 2; }

    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::perror(argv[1]); return 1; }
    std::vector<uint8_t> img(2 * 1024 * 1024, 0xFF);
    std::fread(img.data(), 1, img.size(), f);
    std::fclose(f);

    f = std::fopen(argv[2], "rb");
    if (!f) { std::perror(argv[2]); return 1; }
    std::vector<uint8_t> emu((size_t)NFRAMES * 16384);
    size_t emuN = std::fread(emu.data(), 1, emu.size(), f) / 16384;
    std::fclose(f);

    Sim sim;
    sim.gtrDownloadSparse(img);
    sim.reset();

    FILE* ours = std::fopen("gany_ls_ours.bin", "wb");
    FILE* ramf = std::getenv("GT_DUMP_RAM_STREAM")
                     ? std::fopen(std::getenv("GT_DUMP_RAM_STREAM"), "wb")
                     : nullptr;
    long flips = 0;
    int schedPos = 0, firstBad = -1, badN = 0;
    uint8_t prevPage = sim.dmaCtl() & 0x02;
    uint64_t guard = sim.cycles + (uint64_t)NFRAMES * 3 * 476000ull;

    while (flips < (long)emuN && flips < NFRAMES && sim.cycles < guard) {
        sim.tick();
        uint8_t page = sim.dmaCtl() & 0x02;
        if (page == prevPage) continue;
        prevPage = page;
        // apply schedule entries due at this flip (emulator convention)
        while (schedPos < NSCHED && SCHED[schedPos].flip <= flips) {
            sim.top.joy1 = SCHED[schedPos].joy;
            schedPos++;
        }
        // capture the newly displayed page
        int p = (sim.dmaCtl() >> 1) & 1;
        std::array<uint8_t, 16384> fb;
        for (int i = 0; i < 16384; i++)
            fb[i] = sim.vramRead(p, (uint16_t)i);
        std::fwrite(fb.data(), 1, fb.size(), ours);
        if (ramf) {
            std::array<uint8_t, 0x8000> ram;
            for (int i = 0; i < 0x8000; i++) ram[i] = sim.sysram((uint16_t)i);
            std::fwrite(ram.data(), 1, ram.size(), ramf);
        }
        if (std::memcmp(fb.data(), emu.data() + (size_t)flips * 16384,
                        16384) != 0) {
            if (firstBad < 0) firstBad = (int)flips;
            badN++;
        }
        flips++;
    }
    std::fclose(ours);
    if (ramf) std::fclose(ramf);
    std::printf("flips=%ld matched=%ld divergent=%d firstBad=%d\n",
                flips, flips - badN, badN, firstBad);
    return badN ? 1 : 0;
}

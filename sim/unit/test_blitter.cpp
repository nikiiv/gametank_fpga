// Blitter unit tests: the verilated rtl/blitter.sv against a C++ golden
// model transcribed from the emulator's engine (blitter.cpp CatchUp — same
// phase order), plus IRQ timing (exactly W×H CPU cycles after TRIGGER).
//
// Directed cases cover mirrors, transparency, colorfill, GCARRY tile wrap,
// WRAPX/WRAPY clipping, degenerate W/H, IRQ clear/masking; a seeded
// randomized sweep compares final VRAM, write counts, gram_mid and IRQ
// timing across parameter space.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include <verilated.h>
#include "Vblitter.h"

#define CHECK(cond, ...)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n     ", __FILE__,       \
                         __LINE__, #cond);                                \
            std::fprintf(stderr, __VA_ARGS__);                            \
            std::fprintf(stderr, "\n");                                   \
            std::exit(1);                                                 \
        }                                                                 \
    } while (0)

// ---------------------------------------------------------------------------
// Golden model (transcribed from GameTankEmulator blitter.cpp)
// ---------------------------------------------------------------------------
struct Golden {
    const uint8_t* gram;
    uint8_t vram[1 << 14];
    uint8_t params[8] = {0};
    uint8_t dma = 0, banking = 0;

    uint8_t cW = 0, cVX = 0, cVY = 0, cGX = 0, cGY = 0, cH = 0;
    bool trigger = false, init = false, running = false;
    long writes = 0;
    uint8_t gmid = 0;

    uint8_t gcarry(uint8_t v) const {
        return (dma & 0x10) ? (uint8_t)(v + 1)
                            : (uint8_t)((v & 0xF0) | ((v + 1) & 0x0F));
    }

    void writeParam(uint8_t a, uint8_t v) {
        if ((a & 7) == 6) trigger = v & 1;
        else params[a & 7] = v;
    }

    void step() {
        if (running) --cW;
        if (init) cW = params[4] & 0x7F;
        bool rowc = (cW == 0);

        if (running) {
            ++cVX;
            cGX = gcarry(cGX);
            if (rowc) { ++cVY; cGY = gcarry(cGY); --cH; }
        }
        if (rowc || init) { cVX = params[0]; cGX = params[2]; }
        if (init) { cVY = params[1]; cGY = params[3]; cH = params[5] & 0x7F; }
        if (cH == 0) running = false;
        running = running || init;
        if (rowc) cW = params[4] & 0x7F;
        init = trigger;
        trigger = false;

        if (running) {
            uint8_t gx = (params[4] & 0x80) ? (uint8_t)~cGX : cGX;
            uint8_t gy = (params[5] & 0x80) ? (uint8_t)~cGY : cGY;
            gmid = (uint8_t)((((gy >> 7) & 1) << 1) | ((gx >> 7) & 1));
            uint8_t color;
            if (dma & 0x08) color = (uint8_t)~params[7];
            else {
                uint32_t a = (uint32_t)((banking & 7) << 16) |
                             (uint32_t)((gy & 0x80) << 8)  |   // gy[7] -> bit 15
                             (uint32_t)((gx & 0x80) << 7)  |   // gx[7] -> bit 14
                             (uint32_t)((gy & 0x7F) << 7)  |
                             (uint32_t)(gx & 0x7F);
                color = gram[a];
            }
            if ((dma & 0x01) && ((dma & 0x80) || color != 0)
                && !((cVX & 0x80) && (banking & 0x10))
                && !((cVY & 0x80) && (banking & 0x20))) {
                vram[((cVY & 0x7F) << 7) | (cVX & 0x7F)] = color;
                writes++;
            }
        }
    }
};

// ---------------------------------------------------------------------------
// RTL harness: 8-clk CPU windows, BRAM-latency GRAM model, VRAM capture
// ---------------------------------------------------------------------------
struct Rtl {
    Vblitter top;
    const uint8_t* gram;
    uint8_t vram[1 << 14];
    uint8_t gq_next = 0;
    long    writes = 0;
    long    cpuCycle = 0;   // count of cpu_ce strobes
    long    irqCycle = -1;  // cpu cycle where irq first rose

    void clk1() {
        top.gram_q = gq_next;
        top.clk_sys = 1;
        top.eval();
        if (top.vram_we) { vram[top.vram_addr] = top.vram_din; writes++; }
        gq_next = gram[top.gram_addr];
        top.clk_sys = 0;
        top.eval();
    }

    void reset() {
        top.reset = 1;
        top.cpu_ce = 0;
        top.param_we = 0;
        top.gram_ready = 1;   // BRAM-style always-ready (no DDR3 model here)
        for (int i = 0; i < 8; i++) clk1();
        top.reset = 0;
        std::memset(vram, 0, sizeof vram);
        writes = 0;
        cpuCycle = 0;
        irqCycle = -1;
    }

    // One CPU cycle; optional parameter write (as mainbus issues it: a 1-clk
    // pulse right after the strobe with latched values).
    void cpu(int wAddr = -1, uint8_t wData = 0) {
        top.cpu_ce = 1;
        clk1();
        top.cpu_ce = 0;
        cpuCycle++;
        if (top.irq && irqCycle < 0) irqCycle = cpuCycle;
        if (wAddr >= 0) {
            top.param_we = 1;
            top.param_addr = wAddr & 7;
            top.param_data = wData;
        }
        clk1();
        top.param_we = 0;
        for (int i = 0; i < 6; i++) clk1();
        if (top.irq && irqCycle < 0) irqCycle = cpuCycle;
    }
};

// Run one blit on both models and compare.
static void runCase(const uint8_t* gram, uint8_t dma, uint8_t banking,
                    const uint8_t p[8], const char* label) {
    Golden g;
    g.gram = gram;
    g.dma = dma;
    g.banking = banking;
    std::memset(g.vram, 0, sizeof g.vram);

    Rtl r;
    r.gram = gram;
    r.reset();
    r.top.dma_ctl = dma;
    r.top.banking = banking;

    // Ordering matches the emulator (VDMA_Write: CatchUp THEN SetParam) and
    // the RTL (engine steps at the strobe, the param pulse lands after it):
    // the engine cycle completes before the write becomes visible.
    for (int i = 0; i < 6; i++) {              // VX VY GX GY W H
        r.cpu(i, p[i]);
        g.step();
        g.writeParam(i, p[i]);
    }
    r.cpu(7, p[7]);                            // COLOR
    g.step();
    g.writeParam(7, p[7]);

    r.cpu(6, 0x01);                            // TRIGGER
    g.step();
    g.writeParam(6, 0x01);
    long trigAt = r.cpuCycle;                  // the cycle of the write itself

    long budget = (long)(p[4] & 0x7F) * (p[5] & 0x7F) + (p[5] & 0x7F) + 16;
    for (long i = 0; i < budget; i++) { r.cpu(); g.step(); }

    for (int i = 0; i < (1 << 14); i++) {
        CHECK(r.vram[i] == g.vram[i],
              "%s: VRAM[%04x] (x=%d y=%d) rtl %02x golden %02x "
              "(rtl writes %ld, golden %ld)",
              label, i, i & 127, i >> 7, r.vram[i], g.vram[i],
              r.writes, g.writes);
    }
    CHECK(r.writes == g.writes, "%s: write count %ld vs %ld",
          label, r.writes, g.writes);
    CHECK(r.top.gram_mid == g.gmid, "%s: gram_mid %d vs %d",
          label, (int)r.top.gram_mid, (int)g.gmid);

    if (dma & 0x40) {
        long wh = (long)(p[4] & 0x7F) * (p[5] & 0x7F);
        if (wh > 0) {
            CHECK(r.irqCycle == trigAt + wh, "%s: irq at cycle %ld, want %ld",
                  label, r.irqCycle, trigAt + wh);
        } else {
            // W×H = 0: the emulator schedules an immediate IRQ whose exact
            // ±1 alignment is a scheduler internal no software can observe;
            // just require it promptly.
            CHECK(r.irqCycle >= trigAt && r.irqCycle <= trigAt + 1,
                  "%s: degenerate irq at %ld (trigger %ld)",
                  label, r.irqCycle, trigAt);
        }
    }
}

int main() {
    // GRAM backing: deterministic pseudo-random contents
    static uint8_t gram[1 << 19];
    std::mt19937 rng(0xB117);
    for (auto& b : gram) b = (uint8_t)rng();

    const uint8_t DMA_BASE = 0x01 | 0x40 | 0x80;  // COPY_ENABLE|IRQ|TRANSP

    // --- directed --------------------------------------------------------
    { uint8_t p[8] = {10, 20, 5, 9, 16, 8, 0, 0};
      runCase(gram, DMA_BASE, 0, p, "plain 16x8"); }
    { uint8_t p[8] = {10, 20, 5, 9, (uint8_t)(16|0x80), 8, 0, 0};
      runCase(gram, DMA_BASE, 0, p, "x-mirror"); }
    { uint8_t p[8] = {10, 20, 5, 9, 16, (uint8_t)(8|0x80), 0, 0};
      runCase(gram, DMA_BASE, 0, p, "y-mirror"); }
    { uint8_t p[8] = {0, 0, 200, 130, 32, 32, 0, 0};
      runCase(gram, DMA_BASE, 5, p, "high-quadrant source, bank 5"); }
    { uint8_t p[8] = {30, 30, 4, 4, 24, 24, 0, 0};
      runCase(gram, DMA_BASE & (uint8_t)~0x10U, 0, p, "gcarry tile wrap"); }
    { uint8_t p[8] = {5, 5, 0, 0, 20, 20, 0, 0x3C};
      runCase(gram, DMA_BASE | 0x08, 0, p, "colorfill ~color"); }
    { uint8_t p[8] = {5, 5, 0, 0, 20, 20, 0, 0};
      runCase(gram, (DMA_BASE & (uint8_t)~0x80U) | 0x10, 0, p, "transparency skip"); }
    { uint8_t p[8] = {120, 8, 0, 0, 40, 6, 0, 0};
      runCase(gram, DMA_BASE, 0x10, p, "wrapx clip"); }
    { uint8_t p[8] = {120, 8, 0, 0, 40, 6, 0, 0};
      runCase(gram, DMA_BASE, 0, p, "wrapx wraps when bit clear"); }
    { uint8_t p[8] = {8, 120, 0, 0, 6, 40, 0, 0};
      runCase(gram, DMA_BASE, 0x20, p, "wrapy clip"); }
    { uint8_t p[8] = {10, 10, 0, 0, 16, 0, 0, 0};
      runCase(gram, DMA_BASE, 0, p, "degenerate h=0"); }
    { uint8_t p[8] = {10, 10, 0, 0, 0, 16, 0, 0};
      runCase(gram, DMA_BASE, 0, p, "degenerate w=0"); }
    std::printf("PASS blitter directed (12 cases)\n");

    // --- IRQ clear & masking ---------------------------------------------
    {
        Rtl r;
        r.gram = gram;
        r.reset();
        r.top.dma_ctl = DMA_BASE;
        r.top.banking = 0;
        uint8_t p[8] = {0, 0, 0, 0, 4, 4, 0, 0};
        for (int i = 0; i < 6; i++) r.cpu(i, p[i]);
        r.cpu(6, 0x01);
        for (int i = 0; i < 40; i++) r.cpu();
        CHECK(r.top.irq == 1, "irq pending after blit");
        r.top.dma_ctl = DMA_BASE & (uint8_t)~0x40U;   // mask via COPY_IRQ
        r.cpu();
        CHECK(r.top.irq == 0, "irq masked when COPY_IRQ off");
        r.top.dma_ctl = DMA_BASE;
        r.cpu();
        CHECK(r.top.irq == 1, "irq unmasked again (level, still pending)");
        r.cpu(6, 0x00);                                // TRIGGER write clears
        r.cpu();
        CHECK(r.top.irq == 0, "TRIGGER write clears pending");
        std::printf("PASS blitter irq clear/mask\n");
    }

    // --- randomized sweep --------------------------------------------------
    std::mt19937 prng(0xF00D);
    for (int n = 0; n < 150; n++) {
        uint8_t p[8];
        for (auto& v : p) v = (uint8_t)prng();
        p[4] = (uint8_t)((p[4] & 0x80) | (prng() % 40));   // keep runtimes sane
        p[5] = (uint8_t)((p[5] & 0x80) | (prng() % 40));
        uint8_t dma = (uint8_t)(0x01 | 0x40 | (prng() & (0x08 | 0x10 | 0x80)));
        uint8_t banking = (uint8_t)(prng() & 0x37);        // bank + wrap bits
        char label[32];
        std::snprintf(label, sizeof label, "random #%d", n);
        runCase(gram, dma, banking, p, label);
    }
    std::printf("PASS blitter randomized (150 sweeps)\n");
    return 0;
}

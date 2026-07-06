// Shared Verilator harness for the GameTank core.
//
// The core top (rtl/gametank.sv) is the sim top — no sys/ anywhere. This
// header plays the role sys/ plays on hardware: clocking, reset, and
// sampling the video outputs at posedge clk when ce_pix is high (exactly
// how the MiSTer framework latches CE_PIXEL-qualified video).

#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include <verilated.h>
#include "Vgametank.h"
#include "Vgametank___024root.h"

struct Pixel {
    uint8_t r, g, b;
    bool    de, hs, vs;
};

// Minimal test reporting: CHECK aborts with context on failure.
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

class Sim {
public:
    Vgametank top;
    uint64_t  cycles = 0;

    // Cartridge backing for the abstract cart bus ($8000-$FFFF window).
    // Defaults to 0xFF (floating bus, matching the emu wrapper's tie-off).
    std::vector<uint8_t> cart = std::vector<uint8_t>(32768, 0xFF);

    // DDR3 model backing the GRAM region (0x3000_0000, rtl/gram_ddr.sv).
    // Fixed latency for determinism: a command is accepted when presented,
    // read data starts DDR_LAT ticks later, one 64-bit beat per tick.
    static constexpr int DDR_LAT = 12;
    std::vector<uint8_t> ddr = std::vector<uint8_t>(512 * 1024, 0);
    struct DdrJob { uint64_t due; uint32_t byteAddr; int beats; int beat; };
    std::vector<DdrJob> ddrJobs;

    void ddrStep() {
        top.ddr_dout_ready = 0;
        top.ddr_busy = 0;

        if (!ddrJobs.empty()) {
            auto& j = ddrJobs.front();
            if (cycles >= j.due) {
                uint64_t v = 0;
                uint32_t a = (j.byteAddr + (uint32_t)j.beat * 8) & 0x7FFFF;
                for (int b = 7; b >= 0; b--) v = (v << 8) | ddr[(a + b) & 0x7FFFF];
                top.ddr_dout = v;
                top.ddr_dout_ready = 1;
                if (++j.beat == j.beats) ddrJobs.erase(ddrJobs.begin());
            }
        }

        if (top.ddr_rd && ddrJobs.size() < 4) {
            uint32_t byteAddr = (uint32_t)(top.ddr_addr & 0xFFFF) << 3;
            ddrJobs.push_back({cycles + DDR_LAT, byteAddr, top.ddr_burstcnt, 0});
        }
        if (top.ddr_we) {
            uint32_t byteAddr = (uint32_t)(top.ddr_addr & 0xFFFF) << 3;
            for (int b = 0; b < 8; b++)
                if (top.ddr_be & (1 << b))
                    ddr[(byteAddr + b) & 0x7FFFF] = (uint8_t)(top.ddr_din >> (8 * b));
        }
    }

    void loadCart(const std::string& path) {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) { std::perror(path.c_str()); std::exit(1); }
        size_t n = std::fread(cart.data(), 1, cart.size(), f);
        std::fclose(f);
        if (n == 0) { std::fprintf(stderr, "empty cart %s\n", path.c_str()); std::exit(1); }
    }

    // One clk_sys cycle. onPixel fires at the posedge where ce_pix is high,
    // with output values as they are latched by the framework.
    void tick(const std::function<void(const Pixel&)>& onPixel = nullptr) {
        top.cart_data = cart[top.cart_addr & 0x7FFF];
        ddrStep();

        top.clk_sys = 0;
        top.eval();
        if (top.ce_pix && onPixel) {
            onPixel(Pixel{top.video_r, top.video_g, top.video_b,
                          !(top.hblank || top.vblank),
                          (bool)top.hsync, (bool)top.vsync});
        }
        top.clk_sys = 1;
        top.eval();
        cycles++;
    }

    void reset(int n = 16) {
        top.reset = 1;
        for (int i = 0; i < n; i++) tick();
        top.reset = 0;
    }

    // Test-only visibility into the console (verilator public_flat_rd).
    uint8_t sysram(uint16_t phys) const {
        return top.rootp->gametank__DOT__mainbus__DOT__sysram[phys & 0x7FFF];
    }
    uint8_t banking() const {
        return top.rootp->gametank__DOT__mainbus__DOT__banking;
    }
    uint8_t vramRead(int page, uint16_t addr) const {
        return top.rootp->gametank__DOT__vram__DOT__mem[((page & 1) << 14) |
                                                        (addr & 0x3FFF)];
    }
    uint8_t dmaCtl() const { return top.rootp->gametank__DOT__dma_ctl; }

    // Lockstep support: run until `frames` frame boundaries (the vsync-NMI
    // instant at the start of line 0) have passed, snapshotting the
    // *displayed* framebuffer page (16 KB of palette indices) at each one —
    // the same instant and content the patched emulator dumps.
    std::vector<std::array<uint8_t, 16384>> runFrames(int frames) {
        std::vector<std::array<uint8_t, 16384>> out;
        bool prev = true;  // the reset-time pulse doesn't count as a boundary
        uint64_t guard = cycles + (uint64_t)(frames + 2) * 1816 * 262;
        while ((int)out.size() < frames && cycles < guard) {
            tick();
            bool nmi = top.rootp->gametank__DOT__vsync_nmi;
            if (nmi && !prev && cycles > 100) {
                out.emplace_back();
                int page = (dmaCtl() >> 1) & 1;
                for (int i = 0; i < 16384; i++)
                    out.back()[i] = vramRead(page, (uint16_t)i);
            }
            prev = nmi;
        }
        CHECK((int)out.size() == frames, "only %zu/%d frames captured",
              out.size(), frames);
        return out;
    }
};

struct Frame {
    int width  = 0;
    int height = 0;
    std::vector<uint8_t> rgb;  // width*height*3

    void writePPM(const std::string& path) const {
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) { std::perror(path.c_str()); std::exit(1); }
        std::fprintf(f, "P6\n%d %d\n255\n", width, height);
        std::fwrite(rgb.data(), 1, rgb.size(), f);
        std::fclose(f);
    }
};

// Captures one full frame: from a vsync rising edge to the next, collecting
// DE pixels into rows delimited by hsync rising edges. Asserts a rectangular
// result. Also verifies every line has the same ce_pix-to-ce_pix spacing.
inline Frame captureFrame(Sim& sim) {
    enum class St { WaitVsRise, InFrame };
    St st = St::WaitVsRise;
    bool prevVs = false, prevHs = false;
    bool done = false;

    Frame frame;
    std::vector<uint8_t> row;
    int width = -1;

    // Bound the wait: a frame is 1816 clk × 262 lines (gtvideo raster).
    const uint64_t maxCycles = sim.cycles + 3ull * 1816 * 262;

    while (!done && sim.cycles < maxCycles) {
        sim.tick([&](const Pixel& p) {
            bool vsRise = p.vs && !prevVs;
            bool hsRise = p.hs && !prevHs;
            prevVs = p.vs;
            prevHs = p.hs;

            if (st == St::WaitVsRise) {
                if (vsRise) st = St::InFrame;
                return;
            }

            if (vsRise) { done = true; return; }

            if (hsRise && !row.empty()) {
                if (width < 0) width = (int)row.size() / 3;
                CHECK((int)row.size() / 3 == width,
                      "ragged line %d: %zu px vs width %d",
                      frame.height, row.size() / 3, width);
                frame.rgb.insert(frame.rgb.end(), row.begin(), row.end());
                frame.height++;
                row.clear();
            }
            if (p.de) {
                row.push_back(p.r);
                row.push_back(p.g);
                row.push_back(p.b);
            }
        });
    }
    CHECK(done, "no complete frame within cycle budget");
    frame.width = width;
    return frame;
}

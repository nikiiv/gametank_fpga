// Shared Verilator harness for the GameTank core.
//
// The core top (rtl/gametank.sv) is the sim top — no sys/ anywhere. This
// header plays the role sys/ plays on hardware: clocking, reset, and
// sampling the video outputs at posedge clk when ce_pix is high (exactly
// how the MiSTer framework latches CE_PIXEL-qualified video).

#pragma once

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

class Sim {
public:
    Vgametank top;
    uint64_t  cycles = 0;

    // Cartridge backing for the abstract cart bus ($8000-$FFFF window).
    // Defaults to 0xFF (floating bus, matching the emu wrapper's tie-off).
    std::vector<uint8_t> cart = std::vector<uint8_t>(32768, 0xFF);

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

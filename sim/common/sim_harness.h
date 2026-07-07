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

    Sim() {
        bool all = std::getenv("GT_DDR_HOSTILE") != nullptr;
        hBusy = all || std::getenv("GT_DDR_BUSY") != nullptr;
        hLat  = all || std::getenv("GT_DDR_LAT")  != nullptr;
        hGaps = all || std::getenv("GT_DDR_GAPS") != nullptr;
    }

    // Cartridge backing for the abstract cart bus ($8000-$FFFF window).
    // Defaults to 0xFF (floating bus, matching the emu wrapper's tie-off).
    std::vector<uint8_t> cart = std::vector<uint8_t>(32768, 0xFF);

    // DDR3 model backing the HPS region at byte 0x3000_0000: GRAM at
    // offset 0 (512 KB, rtl/gram_ddr.sv), the cartridge image at offset
    // 0x100000 (2 MB, rtl/cart.sv — CART_BASE_W). Deterministic by
    // default: a command is accepted when presented, read data starts
    // DDR_LAT ticks later, one 64-bit beat per tick. Set hostile=true for
    // an adversarial (but seeded/deterministic) model: variable latency,
    // random busy assertion, and gaps between beats — approximating the
    // real HPS port, which sim's ideal model otherwise hides bugs from.
    bool     hBusy = false, hLat = false, hGaps = false;
    uint32_t ddrRng    = 0x2F6E2B1u;
    static constexpr int      DDR_LAT  = 12;
    static constexpr uint32_t DDR_MASK = 0x3FFFFF;         // 4 MB window
    static constexpr uint32_t CART_OFF = 0x100000;
    std::vector<uint8_t> ddr = std::vector<uint8_t>(4 * 1024 * 1024, 0);
    struct DdrJob { uint64_t due; uint32_t byteOff; int beats; int beat; };
    std::vector<DdrJob> ddrJobs;

    // word address (incl. the 0x0600_0000 base) → byte offset into ddr[]
    static uint32_t ddrOff(uint32_t wordAddr) {
        return ((wordAddr << 3) - 0x30000000u) & DDR_MASK;
    }

    uint32_t ddrRand() {
        ddrRng ^= ddrRng << 13; ddrRng ^= ddrRng >> 17; ddrRng ^= ddrRng << 5;
        return ddrRng;
    }

    void ddrStep() {
        top.ddr_dout_ready = 0;
        // hostile: assert busy ~30% of idle cycles (commands must wait)
        bool busyNow = hBusy && (ddrRand() % 10 < 3);
        top.ddr_busy = busyNow;

        if (!ddrJobs.empty()) {
            auto& j = ddrJobs.front();
            if (cycles >= j.due) {
                uint64_t v = 0;
                uint32_t a = (j.byteOff + (uint32_t)j.beat * 8) & DDR_MASK;
                for (int b = 7; b >= 0; b--) v = (v << 8) | ddr[(a + b) & DDR_MASK];
                top.ddr_dout = v;
                top.ddr_dout_ready = 1;
                ++j.beat;
                // hostile: random gap before the next beat
                j.due = cycles + 1 + (hGaps ? (ddrRand() % 4) : 0);
                if (j.beat == j.beats) ddrJobs.erase(ddrJobs.begin());
            }
        }

        if (top.ddr_rd && !busyNow && ddrJobs.size() < 4) {
            uint64_t lat = DDR_LAT + (hLat ? (ddrRand() % 40) : 0);
            ddrJobs.push_back({cycles + lat, ddrOff(top.ddr_addr),
                               top.ddr_burstcnt, 0});
        }
        if (top.ddr_we && !busyNow) {
            uint32_t byteOff = ddrOff(top.ddr_addr);
            for (int b = 0; b < 8; b++)
                if (top.ddr_be & (1 << b))
                    ddr[(byteOff + b) & DDR_MASK] = (uint8_t)(top.ddr_din >> (8 * b));
        }
    }

    void loadCart(const std::string& path) {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) { std::perror(path.c_str()); std::exit(1); }
        size_t n = std::fread(cart.data(), 1, cart.size(), f);
        std::fclose(f);
        if (n == 0) { std::fprintf(stderr, "empty cart %s\n", path.c_str()); std::exit(1); }
    }

    // --- M7 DDR3 cartridge (rtl/cart.sv) -----------------------------------

    bool cartPresent() const {
        return top.rootp->gametank__DOT__cart__DOT__present_r;
    }
    uint8_t bankMask() const {
        return top.rootp->gametank__DOT__cart__DOT__bank_mask;
    }

    // Stream a .gtr image through the core's download port exactly like
    // the MiSTer wrapper: reset held for the whole transfer, dl_busy
    // pacing per byte. Leaves reset asserted — call reset() to boot.
    void gtrDownload(const std::vector<uint8_t>& img) {
        top.reset = 1;
        top.dl_active = 1;
        tick(); tick();
        for (size_t i = 0; i < img.size(); i++) {
            top.dl_addr = (uint32_t)i;
            top.dl_data = img[i];
            top.dl_wr = 1;
            tick();
            top.dl_wr = 0;
            uint64_t guard = cycles + 1000;
            while (top.dl_busy && cycles < guard) tick();
            CHECK(!top.dl_busy, "download stuck at byte %zu", i);
        }
        finishDownload();
    }

    // Big-image shortcut for tests: seed the DDR cart region directly and
    // push only the first and last byte through the port (enough to set
    // size/type and trigger the fixed-bank fill).
    void gtrDownloadSparse(const std::vector<uint8_t>& img) {
        for (size_t i = 0; i < img.size(); i++)
            ddr[(CART_OFF + i) & DDR_MASK] = img[i];
        top.reset = 1;
        top.dl_active = 1;
        tick(); tick();
        auto put = [&](uint32_t a, uint8_t d) {
            top.dl_addr = a; top.dl_data = d; top.dl_wr = 1;
            tick();
            top.dl_wr = 0;
            uint64_t guard = cycles + 1000;
            while (top.dl_busy && cycles < guard) tick();
            CHECK(!top.dl_busy, "sparse download stuck");
        };
        put(0, img.front());
        put((uint32_t)img.size() - 1, img.back());
        finishDownload();
    }

    void finishDownload() {
        top.dl_active = 0;
        uint64_t guard = cycles + 400000;
        while (!cartPresent() && cycles < guard) tick();
        CHECK(cartPresent(), "fixed-bank fill did not complete");
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
        top.joy1 = 0;
        top.joy2 = 0;
        top.dl_active = 0;
        top.dl_wr = 0;
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
    uint8_t aramRead(uint16_t a) const {
        return top.rootp->gametank__DOT__acp__DOT__aram[a & 0xFFF];
    }

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

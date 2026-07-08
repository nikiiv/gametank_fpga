// Unit test for the DDR3-backed GRAM prefetcher (rtl/gram_ddr.sv): drives
// the blitter-side request interface in exact pixel order (as the engine
// would) against a C++ DDR3 model whose bytes identify their own address,
// and checks every served byte, including stall behavior, row handoffs,
// mirrors, nibble-wrap mode, quadrant crossings, and the CPU channel.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <verilated.h>
#include "Vgram_ddr.h"

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

struct H {
    Vgram_ddr top;
    std::vector<uint8_t> ddr = std::vector<uint8_t>(512 * 1024);
    struct Job { long due; uint32_t addr; int beats; int beat; };
    std::vector<Job> jobs;
    long t = 0;
    int busy_for = 0;

    H() {
        for (size_t i = 0; i < ddr.size(); i++)
            ddr[i] = (uint8_t)(i * 7 + (i >> 9));   // address-identifying
    }

    void clk1() {
        // DDR model (12-tick latency, 1 beat/tick)
        top.ddr_dout_ready = 0;
        bool busy = busy_for > 0;
        if (busy_for > 0) busy_for--;
        top.ddr_busy = busy;
        if (!jobs.empty()) {
            auto& j = jobs.front();
            if (t >= j.due) {
                uint64_t v = 0;
                uint32_t a = (j.addr + (uint32_t)j.beat * 8) & 0x7FFFF;
                for (int b = 7; b >= 0; b--) v = (v << 8) | ddr[(a + b) & 0x7FFFF];
                top.ddr_dout = v;
                top.ddr_dout_ready = 1;
                if (++j.beat == j.beats) jobs.erase(jobs.begin());
            }
        }
        if (top.ddr_rd && !busy && jobs.size() < 4)
            jobs.push_back({t + 12, (uint32_t)(top.ddr_addr & 0xFFFF) << 3,
                            top.ddr_burstcnt, 0});
        if (top.ddr_we && !busy)
            for (int b = 0; b < 8; b++)
                if (top.ddr_be & (1 << b))
                    ddr[(((top.ddr_addr & 0xFFFF) << 3) + b) & 0x7FFFF] =
                        (uint8_t)(top.ddr_din >> (8 * b));

        top.clk_sys = 1; top.eval();
        top.clk_sys = 0; top.eval();
        t++;
    }

    void reset() {
        top.reset = 1;
        top.cpu_ce = 0;
        top.param_we = 0;
        top.blit_want = 0;
        top.cpu_we = top.cpu_rd = 0;
        for (int i = 0; i < 8; i++) clk1();
        top.reset = 0;
    }

    void param(int a, uint8_t v) {   // one CPU cycle with a param pulse
        top.cpu_ce = 1; clk1(); top.cpu_ce = 0;
        top.param_we = 1; top.param_addr = a; top.param_data = v;
        clk1();
        top.param_we = 0;
        for (int i = 0; i < 6; i++) clk1();
    }

    void cpuWrite(uint32_t addr, uint8_t data) {
        long spin = 0;
        while (top.cpu_stall && spin++ < 1000) clk1();
        CHECK(spin < 1000, "cpu write stall stuck");
        top.cpu_addr = addr;
        top.cpu_wdata = data;
        top.cpu_we = 1;
        clk1();
        top.cpu_we = 0;
    }

    uint8_t cpuRead(uint32_t addr, long* stallTicks = nullptr) {
        long pre = 0;
        while (top.cpu_stall && pre++ < 1000) clk1();
        CHECK(pre < 1000, "cpu read pre-stall stuck");
        top.cpu_addr = addr;
        top.cpu_rd = 1;
        clk1();
        top.cpu_rd = 0;
        long spin = 0;
        while (top.cpu_stall && spin++ < 1000) clk1();
        CHECK(spin < 1000, "cpu read stall stuck");
        if (stallTicks) *stallTicks = spin;
        return top.cpu_q;
    }

    uint8_t gcarry(uint8_t v) const {
        return (top.dma_ctl & 0x10) ? (uint8_t)(v + 1)
                                    : (uint8_t)((v & 0xF0) | ((v + 1) & 0x0F));
    }

    // Consume one blit byte at the given effective address; returns it.
    uint8_t consume(uint32_t paddr, long maxCpu = 500) {
        top.blit_paddr = paddr;
        top.blit_want = 1;
        for (long i = 0; i < maxCpu; i++) {
            top.cpu_ce = 1;
            top.eval();                 // settle ready for this candidate
            bool step = top.blit_ready;
            clk1();
            top.cpu_ce = 0;
            if (step) {
                top.blit_addr = paddr;  // engine registers at the step
                top.blit_want = 0;
                for (int k = 0; k < 7; k++) clk1();
                return top.blit_q;
            }
            for (int k = 0; k < 7; k++) clk1();
        }
        CHECK(false, "starved forever at %05x", paddr);
        return 0;
    }
};

static uint32_t gaddr(int bank, uint8_t gy, uint8_t gx) {
    return ((uint32_t)(bank & 7) << 16) | ((uint32_t)(gy & 0x80) << 8) |
           ((uint32_t)(gx & 0x80) << 7) | ((uint32_t)(gy & 0x7F) << 7) |
           (gx & 0x7F);
}

// Run a full simulated blit consumption in exact engine order.
static void runBlit(H& h, int bank, uint8_t gx0, uint8_t gy0, int w, int h_,
                    bool xdir, bool ydir, uint8_t dma, const char* label) {
    h.top.dma_ctl = dma;
    h.top.banking = (uint8_t)bank;
    h.param(2, gx0);
    h.param(3, gy0);
    h.param(4, (uint8_t)(w | (xdir ? 0x80 : 0)));
    h.param(5, (uint8_t)(h_ | (ydir ? 0x80 : 0)));
    h.param(6, 0x01);

    uint8_t gy = gy0;
    for (int r = 0; r < h_; r++) {
        uint8_t gx = gx0;
        for (int c = 0; c < w; c++) {
            uint8_t egx = xdir ? (uint8_t)~gx : gx;
            uint8_t egy = ydir ? (uint8_t)~gy : gy;
            uint32_t a = gaddr(bank, egy, egx);
            uint8_t got = h.consume(a);
            if (got != h.ddr[a] && std::getenv("GDBG")) {
                std::printf("%s r%d c%d addr %05x got %02x want %02x -- row dump:\n",
                            label, r, c, a, got, h.ddr[a]);
                for (int k = 0; k < 128; k++) {
                    uint32_t aa = (a & ~0x7Fu) | (uint32_t)k;
                    uint8_t g2 = h.consume(aa);
                    if (g2 != h.ddr[aa])
                        std::printf("  col %3d: got %02x want %02x (val-of col %d?)\n",
                                    k, g2, h.ddr[aa], -1);
                }
                std::exit(1);
            }
            CHECK(got == h.ddr[a], "%s r%d c%d addr %05x: got %02x want %02x",
                  label, r, c, a, got, h.ddr[a]);
            gx = h.gcarry(gx);
        }
        gy = h.gcarry(gy);
    }
}

int main() {
    H h;
    h.reset();

    const uint8_t DMA = 0x01 | 0x10;   // COPY_ENABLE | GCARRY(full)

    runBlit(h, 0, 0, 0, 16, 16, false, false, DMA, "plain 16x16");
    runBlit(h, 0, 0, 0, 16, 16, false, false, DMA & ~0x10, "nibble mode");
    runBlit(h, 3, 40, 20, 24, 8, false, false, DMA, "bank3 offset");
    runBlit(h, 0, 0xF0, 0, 16, 8, true, false, DMA, "x-mirror");
    runBlit(h, 0, 0, 0xF0, 8, 16, false, true, DMA, "y-mirror");
    runBlit(h, 1, 120, 5, 16, 4, false, false, DMA, "quadrant crossing");
    runBlit(h, 0, 0, 100, 4, 30, false, false, DMA, "narrow tall");
    std::printf("PASS gram_ddr blit streams\n");

    // CPU channel: write then read back through DDR3
    h.top.cpu_addr = gaddr(2, 33, 44);
    h.top.cpu_wdata = 0x77;
    h.top.cpu_we = 1;
    h.clk1();
    h.top.cpu_we = 0;
    long spin = 0;
    while (h.top.cpu_stall && spin++ < 2000) h.clk1();
    CHECK(spin < 2000, "cpu write stall released");
    for (int i = 0; i < 40; i++) h.clk1();
    CHECK(h.ddr[gaddr(2, 33, 44)] == 0x77, "cpu write landed");

    h.top.cpu_rd = 1;
    h.clk1();
    h.top.cpu_rd = 0;
    spin = 0;
    while (h.top.cpu_stall && spin++ < 200) h.clk1();
    CHECK(spin < 200, "cpu read stall released");
    CHECK(h.top.cpu_q == 0x77, "cpu readback: %02x", h.top.cpu_q);
    std::printf("PASS gram_ddr cpu channel\n");

    long s0 = 0, s1 = 0;
    uint32_t line = gaddr(1, 22, 32);
    uint8_t r0 = h.cpuRead(line + 0, &s0);
    uint8_t r1 = h.cpuRead(line + 1, &s1);
    CHECK(r0 == h.ddr[line + 0], "cached read fill byte0: %02x", r0);
    CHECK(r1 == h.ddr[line + 1], "cached read hit byte1: %02x", r1);
    CHECK(s0 > 0, "first read should miss/fill");
    CHECK(s1 == 0, "same DDR word read should hit without stall (%ld)", s1);
    h.cpuWrite(line + 2, 0xEE);
    uint8_t r2 = h.cpuRead(line + 2, &s1);
    CHECK(r2 == 0xEE, "cached write-hit readback: %02x", r2);
    CHECK(s1 == 0, "write-updated cached line should read without stall");
    std::printf("PASS gram_ddr cpu read cache\n");

    // A spritesheet upload is a long stream of CPU GRAM-window writes. If a
    // DDR write command is backpressured, the core must stall the CPU rather
    // than overwrite the single pending write slot with later bytes.
    const uint32_t base = gaddr(0, 48, 0);
    h.busy_for = 160;
    for (int i = 0; i < 16; i++)
        h.cpuWrite(base + (uint32_t)i, (uint8_t)(0xA0 + i));
    for (int i = 0; i < 300; i++) h.clk1();
    int bad = 0;
    for (int i = 0; i < 16; i++) {
        uint8_t got = h.ddr[base + (uint32_t)i];
        uint8_t want = (uint8_t)(0xA0 + i);
        if (got != want && bad++ < 4)
            std::fprintf(stderr, "posted write[%d]: %02x want %02x\n",
                         i, got, want);
    }
    CHECK(bad == 0, "%d CPU GRAM-window writes lost under DDR backpressure",
          bad);
    std::printf("PASS gram_ddr cpu write backpressure\n");
    return 0;
}

// M3 integration test: the fb_pattern cart draws x^y into framebuffer page 0
// through the VDMA window; we capture the scanned-out DE frame and compare
// the 640×246 game area pixel-exactly (plus CRC) against the same pattern
// pushed through the emulator's capture-based palette. Also checks the
// authentic border-latch behavior and the vsync-NMI frame counter.
//
// Raster mapping (docs/HARDWARE.md §Video): DE is 748×246; fb pixel x spans
// DE columns 92+5x..96+5x; DE row d shows fb row d>>1 (rows 0-122).

#include "sim_harness.h"
#include "gt_palette.h"

static uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFF;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & (-(c & 1)));
    }
    return ~c;
}

static const uint8_t* pal(uint8_t idx) { return gt_palette[idx]; }

int main() {
    Sim sim;
    sim.loadCart("../testroms/fb_pattern.gtr");
    sim.reset();

    // Fill takes ~250k CPU cycles (~2M clk); add two frames of margin.
    for (int i = 0; i < 2'500'000; i++) sim.tick();

    Frame f = captureFrame(sim);
    f.writePPM("fb_pattern.ppm");  // reference artifact for hardware bring-up
    CHECK(f.width == 748 && f.height == 246, "frame is %dx%d", f.width, f.height);

    // Golden game area: 640×246 RGB
    std::vector<uint8_t> want, got;
    want.reserve(640 * 246 * 3);
    got.reserve(640 * 246 * 3);
    for (int d = 0; d < 246; d++) {
        int row = d >> 1;
        for (int x = 0; x < 128; x++) {
            const uint8_t* c = pal((uint8_t)(x ^ row));
            for (int k = 0; k < 5; k++) {
                want.insert(want.end(), c, c + 3);
                const uint8_t* q = &f.rgb[(size_t)(d * f.width + 92 + 5 * x + k) * 3];
                got.insert(got.end(), q, q + 3);
            }
        }
    }
    if (want != got) {
        for (size_t i = 0; i < want.size(); i += 3) {
            if (want[i] != got[i] || want[i+1] != got[i+1] || want[i+2] != got[i+2]) {
                size_t px = i / 3;
                f.writePPM("fb_pattern_fail.ppm");
                CHECK(false, "first mismatch at game px (%zu,%zu): got %02x%02x%02x want %02x%02x%02x (frame dumped)",
                      px % 640, px / 640, got[i], got[i+1], got[i+2],
                      want[i], want[i+1], want[i+2]);
            }
        }
    }
    uint32_t crc = crc32(got.data(), got.size());
    CHECK(crc == crc32(want.data(), want.size()), "CRC mismatch");

    // Border latch (74564 behavior): on DE row 2r the left border shows
    // pixel 127 of row r-1; the right border always shows the current row's
    // pixel 127. Row value at (127, r) = 127 ^ r.
    {
        int r = 20;
        const uint8_t* lb = &f.rgb[(size_t)((2*r) * f.width + 10) * 3];
        const uint8_t* wl = pal((uint8_t)(127 ^ (r - 1)));
        CHECK(lb[0]==wl[0] && lb[1]==wl[1] && lb[2]==wl[2],
              "left border row %d: %02x%02x%02x want %02x%02x%02x",
              2*r, lb[0],lb[1],lb[2], wl[0],wl[1],wl[2]);
        const uint8_t* rb = &f.rgb[(size_t)((2*r) * f.width + 740) * 3];
        const uint8_t* wr = pal((uint8_t)(127 ^ r));
        CHECK(rb[0]==wr[0] && rb[1]==wr[1] && rb[2]==wr[2],
              "right border row %d: %02x%02x%02x want %02x%02x%02x",
              2*r, rb[0],rb[1],rb[2], wr[0],wr[1],wr[2]);
    }

    // Vsync NMI: the cart's WAI loop increments $10 once per frame.
    CHECK(sim.sysram(0x0010) >= 2, "NMI frame counter: %02x", sim.sysram(0x0010));

    std::printf("PASS cart_fb_pattern: 640x246 game area exact (crc %08x), "
                "borders authentic, %d NMIs\n", crc, sim.sysram(0x0010));
    return 0;
}

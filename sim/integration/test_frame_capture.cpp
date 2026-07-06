// Integration test: capture full frames from the core's video output the way
// the MiSTer framework would, and check the M1 test pattern's content.
//
// Expected pattern (rtl/testpattern.sv): 360x240 active, white 1-px border,
// color bars (45 px each) for y<160, grayscale ramp 160<=y<200, checkerboard
// below. Two consecutive frames must be identical (checker scrolls only
// every 16 frames — determinism check).

#include "sim_harness.h"

static const uint8_t* at(const Frame& f, int x, int y) {
    return &f.rgb[(size_t)(y * f.width + x) * 3];
}

static void expectPx(const Frame& f, int x, int y,
                     uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t* p = at(f, x, y);
    CHECK(p[0] == r && p[1] == g && p[2] == b,
          "pixel (%d,%d) = %02x%02x%02x, want %02x%02x%02x",
          x, y, p[0], p[1], p[2], r, g, b);
}

int main() {
    Sim sim;
    sim.reset();

    Frame f1 = captureFrame(sim);
    CHECK(f1.width == 360 && f1.height == 240,
          "frame is %dx%d", f1.width, f1.height);

    // Border
    expectPx(f1, 0, 0, 0xFF, 0xFF, 0xFF);
    expectPx(f1, 359, 239, 0xFF, 0xFF, 0xFF);
    expectPx(f1, 180, 0, 0xFF, 0xFF, 0xFF);
    expectPx(f1, 0, 120, 0xFF, 0xFF, 0xFF);

    // Color bars (45 px wide): 0 white, 1 yellow, 6 blue
    expectPx(f1, 22, 80, 0xBF, 0xBF, 0xBF);
    expectPx(f1, 67, 80, 0xBF, 0xBF, 0x00);
    expectPx(f1, 292, 80, 0x00, 0x00, 0xBF);

    // Grayscale ramp: r=g=b=x for x<256
    expectPx(f1, 100, 180, 100, 100, 100);
    expectPx(f1, 200, 180, 200, 200, 200);

    // Checkerboard (y=220 has vc[4]=1, frame[4]=0): x[4]=0 -> green, x[4]=1 -> black
    expectPx(f1, 8, 220, 0x50, 0xA0, 0x50);
    expectPx(f1, 20, 220, 0x00, 0x00, 0x00);

    f1.writePPM("frame1.ppm");

    Frame f2 = captureFrame(sim);
    CHECK(f2.width == f1.width && f2.height == f1.height,
          "frame 2 is %dx%d", f2.width, f2.height);
    CHECK(f1.rgb == f2.rgb, "consecutive frames differ");

    std::printf("PASS frame_capture: %dx%d, 2 identical frames, frame1.ppm written\n",
                f1.width, f1.height);
    return 0;
}

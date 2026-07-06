// Unit test: raster timing of the M1 test pattern generator.
//
// Checks against the numbers in rtl/testpattern.sv: ce_pix every 4 clk,
// 455 pixels per line, 262 lines per frame, 360x240 active window.

#include "sim_harness.h"

int main() {
    Sim sim;
    sim.reset();

    const int H_TOTAL = 455, V_TOTAL = 262;
    const int H_ACTIVE = 360, V_ACTIVE = 240;

    uint64_t lastPixelCycle = 0;
    long     pixelIndex = -1;

    long lastHsRisePixel = -1;
    long lastVsRisePixel = -1;
    long hsyncsSinceVs = 0;
    bool prevHs = false, prevVs = false;

    int  linePixels = 0;       // DE pixels since last hsync rise
    int  activeLines = 0;      // lines with any DE pixels, current frame
    long framesChecked = 0;

    // ~3 frames of clk cycles
    for (uint64_t c = 0; c < 3ull * 4 * H_TOTAL * V_TOTAL; c++) {
        sim.tick([&](const Pixel& p) {
            pixelIndex++;

            // ce_pix cadence: exactly every 4 clk
            if (pixelIndex > 0) {
                CHECK(sim.cycles - lastPixelCycle == 4,
                      "ce_pix spacing %llu clk at pixel %ld",
                      (unsigned long long)(sim.cycles - lastPixelCycle),
                      pixelIndex);
            }
            lastPixelCycle = sim.cycles;

            bool hsRise = p.hs && !prevHs;
            bool vsRise = p.vs && !prevVs;
            prevHs = p.hs;
            prevVs = p.vs;

            if (p.de) linePixels++;

            if (hsRise) {
                if (lastHsRisePixel >= 0) {
                    CHECK(pixelIndex - lastHsRisePixel == H_TOTAL,
                          "line length %ld px", pixelIndex - lastHsRisePixel);
                    if (linePixels) {
                        CHECK(linePixels == H_ACTIVE,
                              "active line has %d px", linePixels);
                        activeLines++;
                    }
                }
                lastHsRisePixel = pixelIndex;
                linePixels = 0;
                hsyncsSinceVs++;
            }

            if (vsRise) {
                if (lastVsRisePixel >= 0) {
                    CHECK(pixelIndex - lastVsRisePixel == (long)H_TOTAL * V_TOTAL,
                          "frame length %ld px", pixelIndex - lastVsRisePixel);
                    CHECK(hsyncsSinceVs == V_TOTAL,
                          "%ld hsyncs per frame", hsyncsSinceVs);
                    CHECK(activeLines == V_ACTIVE,
                          "%d active lines per frame", activeLines);
                    framesChecked++;
                }
                lastVsRisePixel = pixelIndex;
                hsyncsSinceVs = 0;
                activeLines = 0;
            }
        });
    }

    CHECK(framesChecked >= 2, "only %ld full frames measured", framesChecked);
    std::printf("PASS video_timing: %ld frames, %ld pixels\n",
                framesChecked, pixelIndex + 1);
    return 0;
}

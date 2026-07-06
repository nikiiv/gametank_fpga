// Unit test: gtvideo raster timing against the schematic-derived numbers
// (docs/HARDWARE.md §Video): 227 CPU cycles (1816 clk) per line, 262
// progressive lines, ce_pix = clk/2 (908 dots/line), DE = 748×246
// (dots 160-907, lines 16-261), vsync lines 4-7, NMI pulse at line 0.

#include "sim_harness.h"

int main() {
    Sim sim;
    sim.reset();

    const int DOTS_PER_LINE = 908;
    const int LINES = 262;
    const int DE_W = 748, DE_H = 246;

    uint64_t lastDotCycle = 0;
    long dotIndex = -1;

    long lastHsRiseDot = -1, lastVsRiseDot = -1;
    long hsyncsSinceVs = 0;
    bool prevHs = false, prevVs = false;
    int  linePixels = 0, activeLines = 0;
    long framesChecked = 0;

    // NMI pulse tracking (via top port after gating? gated NMI needs
    // dma_ctl[2]; the raw pulse is internal, so measure vsync spacing here
    // and cover NMI end-to-end in the cart integration test).
    for (uint64_t c = 0; c < 3ull * 1816 * 262 + 100; c++) {
        sim.tick([&](const Pixel& p) {
            dotIndex++;
            if (dotIndex > 0) {
                CHECK(sim.cycles - lastDotCycle == 2,
                      "ce_pix spacing %llu clk at dot %ld",
                      (unsigned long long)(sim.cycles - lastDotCycle), dotIndex);
            }
            lastDotCycle = sim.cycles;

            bool hsRise = p.hs && !prevHs;
            bool vsRise = p.vs && !prevVs;
            prevHs = p.hs;
            prevVs = p.vs;

            if (p.de) linePixels++;

            if (hsRise) {
                if (lastHsRiseDot >= 0) {
                    CHECK(dotIndex - lastHsRiseDot == DOTS_PER_LINE,
                          "line length %ld dots", dotIndex - lastHsRiseDot);
                    if (linePixels) {
                        CHECK(linePixels == DE_W, "active line %d px", linePixels);
                        activeLines++;
                    }
                }
                lastHsRiseDot = dotIndex;
                linePixels = 0;
                hsyncsSinceVs++;
            }

            if (vsRise) {
                if (lastVsRiseDot >= 0) {
                    CHECK(dotIndex - lastVsRiseDot == (long)DOTS_PER_LINE * LINES,
                          "frame length %ld dots", dotIndex - lastVsRiseDot);
                    CHECK(hsyncsSinceVs == LINES, "%ld hsyncs/frame", hsyncsSinceVs);
                    CHECK(activeLines == DE_H, "%d active lines", activeLines);
                    framesChecked++;
                }
                lastVsRiseDot = dotIndex;
                hsyncsSinceVs = 0;
                activeLines = 0;
            }
        });
    }

    CHECK(framesChecked >= 1, "only %ld full frames measured", framesChecked);
    std::printf("PASS video_timing: %ld frames of %dx%d @ 908x262 dots\n",
                framesChecked, DE_W, DE_H);
    return 0;
}

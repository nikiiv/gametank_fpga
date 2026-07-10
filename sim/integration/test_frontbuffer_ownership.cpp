// Minicraft flicker regression: a legitimate mid-frame page flip releases
// the old frontbuffer for drawing. Scanout must stop using that page before
// the first write lands, even though normal page selection is frame-stable
// to hide short $2007 transients (test_vidpage_latch).
//
// Both pages start as $22. During active video the NMI handler selects page 1
// for display, selects page 0 for drawing, then repaints rows 64..79 with
// $11. A stale whole-frame page latch exposes that repaint as a horizontal
// band; a correct ownership handoff keeps the captured frame uniformly $22.

#include "sim_harness.h"

#include <map>

int main(int, char**) {
    std::vector<uint8_t> image(32768, 0xFF);

    const uint32_t codeOffset = 0x6000;  // cartridge $E000
    const std::vector<uint8_t> program = {
        0xA9, 0x20, 0x8D, 0x07, 0x20,        // CPU_TO_VRAM, display page 0
        0xA9, 0x00, 0x8D, 0x05, 0x20,        // draw page 0
        0xA9, 0x00, 0x85, 0x00,
        0xA9, 0x40, 0x85, 0x01,              // ptr = $4000
        0xA0, 0x00,
        0xA9, 0x22,
        0x91, 0x00,                          // fill page 0
        0xC8, 0xD0, 0xFB,
        0xE6, 0x01,
        0xA6, 0x01, 0xE0, 0x80, 0xD0, 0xF3,

        0xA9, 0x08, 0x8D, 0x05, 0x20,        // draw page 1
        0xA9, 0x00, 0x85, 0x00,
        0xA9, 0x40, 0x85, 0x01,
        0xA0, 0x00,
        0xA9, 0x22,
        0x91, 0x00,                          // fill page 1
        0xC8, 0xD0, 0xFB,
        0xE6, 0x01,
        0xA6, 0x01, 0xE0, 0x80, 0xD0, 0xF3,

        0xA9, 0x24, 0x8D, 0x07, 0x20,        // page 0 + NMI + CPU_TO_VRAM
        0xCB,                                // WAI
        0x80, 0xFD,                          // BRA WAI
    };
    std::copy(program.begin(), program.end(), image.begin() + codeOffset);

    const uint32_t nmiOffset = 0x6100;   // cartridge $E100
    const std::vector<uint8_t> nmi = {
        0xA2, 0x04,                          // delay ~5k CPU cycles from NMI
        0xA0, 0x00,
        0x88, 0xD0, 0xFD,
        0xCA, 0xD0, 0xF8,

        0xA9, 0x26, 0x8D, 0x07, 0x20,        // completed page 1 is front
        0xA9, 0x00, 0x8D, 0x05, 0x20,        // released page 0 is back
        0xA9, 0x00, 0x85, 0x00,
        0xA9, 0x60, 0x85, 0x01,              // ptr = $6000 (VRAM row 64)
        0xA0, 0x00,
        0xA9, 0x11,
        0x91, 0x00,                          // repaint rows 64..79
        0xC8, 0xD0, 0xFB,
        0xE6, 0x01,
        0xA6, 0x01, 0xE0, 0x68, 0xD0, 0xF3,
        0x40,                                // RTI
    };
    std::copy(nmi.begin(), nmi.end(), image.begin() + nmiOffset);

    image[0x7FFA] = 0x00; image[0x7FFB] = 0xE1;
    image[0x7FFC] = 0x00; image[0x7FFD] = 0xE0;
    image[0x7FFE] = 0x00; image[0x7FFF] = 0xE0;

    Sim sim;
    sim.gtrDownload(image);
    sim.reset();

    uint64_t guard = 40'000'000;
    while (sim.dmaCtl() != 0x24 && guard--)
        sim.tick();
    CHECK(sim.dmaCtl() == 0x24, "setup completed");

    Frame frame = captureFrame(sim);
    CHECK(frame.width > 0 && frame.height > 0, "frame captured");

    std::map<uint32_t, int> colors;
    for (size_t i = 0; i + 2 < frame.rgb.size(); i += 3) {
        const uint32_t rgb = (uint32_t)frame.rgb[i] << 16 |
                             (uint32_t)frame.rgb[i + 1] << 8 |
                             frame.rgb[i + 2];
        colors[rgb]++;
    }
    if (colors.size() != 1) {
        frame.writePPM("frontbuffer_ownership_fail.ppm");
        for (const auto& [rgb, count] : colors)
            std::printf("  color %06x x%d\n", rgb, count);
    }
    CHECK(colors.size() == 1,
          "released frontbuffer writes leaked into scanout (%zu colors)",
          colors.size());

    std::printf("PASS frontbuffer_ownership: drawing released page does not "
                "leak into scanout\n");
    return 0;
}

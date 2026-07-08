// M8 Ganymede flicker, part 2: VID_OUT_PAGE ($2007 bit 1) must be sampled
// once per frame like the emulator presents it (refreshScreen samples
// dma_control at host frame rate), not muxed live into scanout. Ganymede's
// engine housekeeping rewrites $2007 with the page bit cleared for a few
// thousand CPU cycles mid-frame every other frame; a live mux paints the
// mid-composition page as a horizontal band (visible flicker / sprite
// pop-in on hardware, invisible on the emulator).
//
// Cart: fills VRAM page 0 with $11, page 1 with $22, displays page 1 with
// the vsync NMI enabled; the NMI handler waits ~25k CPU cycles into active
// video, transiently shows page 0 for ~4k cycles, and restores page 1.
// The captured output frame must be uniform (all page-1 pixels).
#include "sim_harness.h"
#include <map>

int main(int, char**) {
    std::vector<uint8_t> img(32768, 0xFF);   // 32 KB EEPROM

    const uint32_t CODE = 0x6000;            // -> $E000
    std::vector<uint8_t> prog = {
        0xA9, 0x00, 0x8D, 0x05, 0x20,        // banking: CPU VRAM page 0
        0xA9, 0x20, 0x8D, 0x07, 0x20,        // $2007: CPU_TO_VRAM, page 0
        0xA9, 0x00, 0x85, 0x00,
        0xA9, 0x40, 0x85, 0x01,              // ptr = $4000
        0xA0, 0x00,                          // LDY #0
        0xA9, 0x11,                          // LDA #$11
        0x91, 0x00,                          // fill1: STA (ptr),Y
        0xC8, 0xD0, 0xFB,                    // INY; BNE fill1
        0xE6, 0x01,                          // INC ptr+1
        0xA6, 0x01, 0xE0, 0x80, 0xD0, 0xF3,  // to $8000? loop while != $80
        0xA9, 0x08, 0x8D, 0x05, 0x20,        // banking: CPU VRAM page 1
        0xA9, 0x00, 0x85, 0x00,
        0xA9, 0x40, 0x85, 0x01,
        0xA0, 0x00,
        0xA9, 0x22,
        0x91, 0x00,                          // fill2
        0xC8, 0xD0, 0xFB,
        0xE6, 0x01,
        0xA6, 0x01, 0xE0, 0x80, 0xD0, 0xF3,
        0xA9, 0x26, 0x8D, 0x07, 0x20,        // show page 1, NMI enable
        0xCB,                                // WAI
        0x80, 0xFD,                          // BRA WAI
    };
    std::copy(prog.begin(), prog.end(), img.begin() + CODE);

    const uint32_t NMI = 0x6100;             // -> $E100
    std::vector<uint8_t> nmi = {
        0xA2, 0x14,                          // LDX #20   (~25.6k cycles)
        0xA0, 0x00,                          // outer: LDY #0
        0x88, 0xD0, 0xFD,                    // inner: DEY; BNE inner
        0xCA, 0xD0, 0xF8,                    // DEX; BNE outer
        0xA9, 0x24, 0x8D, 0x07, 0x20,        // transient: show page 0
        0xA2, 0x03,                          // LDX #3    (~3.8k cycles)
        0xA0, 0x00,
        0x88, 0xD0, 0xFD,
        0xCA, 0xD0, 0xF8,
        0xA9, 0x26, 0x8D, 0x07, 0x20,        // restore page 1
        0x40,                                // RTI
    };
    std::copy(nmi.begin(), nmi.end(), img.begin() + NMI);

    img[0x7FFA] = 0x00; img[0x7FFB] = 0xE1;  // NMI  -> $E100
    img[0x7FFC] = 0x00; img[0x7FFD] = 0xE0;  // RESET-> $E000
    img[0x7FFE] = 0x00; img[0x7FFF] = 0xE0;  // IRQ (unused)

    Sim sim;
    sim.gtrDownload(img);
    sim.reset();

    // run to the end of setup ($2007 = $26), then let two NMI cycles pass
    uint64_t guard = 40'000'000;
    while (sim.dmaCtl() != 0x26 && guard--) sim.tick();
    CHECK(sim.dmaCtl() == 0x26, "setup completed");
    for (int i = 0; i < 3 * 476000; i++) sim.tick();

    Frame f = captureFrame(sim);
    CHECK(f.width > 0 && f.height > 0, "frame captured");

    std::map<uint32_t, int> colors;
    for (size_t i = 0; i + 2 < f.rgb.size(); i += 3)
        colors[(uint32_t)f.rgb[i] << 16 | (uint32_t)f.rgb[i + 1] << 8 |
               f.rgb[i + 2]]++;
    if (colors.size() != 1) {
        f.writePPM("vidpage_latch_fail.ppm");
        for (auto& kv : colors)
            std::printf("  color %06x x%d\n", kv.first, kv.second);
    }
    CHECK(colors.size() == 1,
          "mid-frame $2007 page transient leaked into scanout "
          "(%zu distinct colors)", colors.size());

    std::printf("PASS vidpage_latch: mid-frame page transient never "
                "reaches the display\n");
    return 0;
}

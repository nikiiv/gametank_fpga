// Integration test for the pads_demo cart: scripted joystick input in, the
// on-screen button indicator blocks out. Verifies the whole input path
// (pads FFs -> CPU decode -> framebuffer -> scanout) that the manual
// hardware pad check uses.
//
// Layout (pads_demo.s): 10x10 blocks at x=8+14i, pad1 y=48, pad2 y=80;
// order R,L,D,U,A,B,C,Start; BRIGHT=$1D pressed, DIM=$03 released.

#include "sim_harness.h"
#include "gt_palette.h"

static void checkBlock(const Frame& f, int i, int fbY, bool pressed,
                       const char* label) {
    int fbX = 12 + 14 * i;            // inside block i
    int col = 92 + 5 * fbX + 2;
    int row = 2 * fbY + 10;           // fb y + 5 rows into the block, doubled
    const uint8_t* px = &f.rgb[(size_t)(row * f.width + col) * 3];
    const uint8_t* want = gt_palette[pressed ? 0x1D : 0x03];
    CHECK(px[0] == want[0] && px[1] == want[1] && px[2] == want[2],
          "%s block %d: %02x%02x%02x want %02x%02x%02x (pressed=%d)",
          label, i, px[0], px[1], px[2], want[0], want[1], want[2], pressed);
}

int main() {
    Sim sim;
    sim.loadCart("../testroms/pads_demo.gtr");
    sim.reset();
    sim.top.joy1 = (1 << 4) | (1 << 3);   // A + Up
    sim.top.joy2 = (1 << 0) | (1 << 7);   // Right + Start

    for (int i = 0; i < 3 * 1816 * 262; i++) sim.tick();  // a few frames
    Frame f = captureFrame(sim);
    CHECK(f.width == 748 && f.height == 246, "frame %dx%d", f.width, f.height);

    bool p1[8] = {0, 0, 0, 1, 1, 0, 0, 0};   // U, A
    bool p2[8] = {1, 0, 0, 0, 0, 0, 0, 1};   // R, Start
    for (int i = 0; i < 8; i++) {
        checkBlock(f, i, 48, p1[i], "pad1");
        checkBlock(f, i, 80, p2[i], "pad2");
    }

    // change input mid-run: indicators must follow next frame
    sim.top.joy1 = (1 << 1);              // Left only
    sim.top.joy2 = 0;
    for (int i = 0; i < 3 * 1816 * 262; i++) sim.tick();
    Frame g = captureFrame(sim);
    bool q1[8] = {0, 1, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 8; i++) {
        checkBlock(g, i, 48, q1[i], "pad1(2)");
        checkBlock(g, i, 80, false, "pad2(2)");
    }

    // M6 tone through the M7 core: any held button must produce a moving
    // DAC (the ~424 Hz saw); releasing must freeze it (rate run bit clear).
    sim.top.joy1 = (1 << 4);              // A held
    for (int i = 0; i < 2 * 1816 * 262; i++) sim.tick();  // settle
    auto dacChanges = [&](int ticks) {
        int changes = 0;
        uint8_t last = (uint8_t)(sim.top.audio_l >> 8);
        for (int i = 0; i < ticks; i++) {
            sim.tick();
            uint8_t v = (uint8_t)(sim.top.audio_l >> 8);
            if (v != last) { changes++; last = v; }
        }
        return changes;
    };
    int held = dacChanges(200000);
    CHECK(held > 100, "no tone while button held (%d DAC steps)", held);
    sim.top.joy1 = 0;
    for (int i = 0; i < 2 * 1816 * 262; i++) sim.tick();
    int idle = dacChanges(200000);
    CHECK(idle == 0, "tone did not stop on release (%d DAC steps)", idle);

    std::printf("PASS cart_pads_demo: indicators track scripted input, tone on hold\n");
    return 0;
}

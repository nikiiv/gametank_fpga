// Regression test for the M7 OSD-load freeze: a .gtr download arriving
// while the console is running (the OSD path) must cleanly reset into the
// new game. The wrapper can only hold reset while ioctl_download is high —
// this test releases reset the moment the transfer ends, exactly like the
// wrapper, and the core must keep the console parked until its fixed-bank
// fill is done (dl_wait) rather than booting the boot cart and switching
// the window mid-execution.

#include "sim_harness.h"

int main() {
    Sim sim;

    // boot the external boot-cart path first (console running)
    sim.loadCart("../testroms/ram_pattern.gtr");
    sim.reset();
    for (int i = 0; i < 64000; i++) sim.tick();
    CHECK(sim.sysram(0x1FFF) == 0xC3, "boot cart didn't run: %02x",
          sim.sysram(0x1FFF));

    // now stream a different cart in with wrapper-accurate reset timing:
    // reset high only while dl_active, dropped immediately at the end
    FILE* f = std::fopen("../testroms/fb_pattern.gtr", "rb");
    if (!f) { std::perror("fb_pattern.gtr"); return 1; }
    std::vector<uint8_t> img(32768);
    CHECK(std::fread(img.data(), 1, img.size(), f) == img.size(), "short read");
    std::fclose(f);

    sim.top.reset = 1;
    sim.top.dl_active = 1;
    sim.tick(); sim.tick();
    for (size_t i = 0; i < img.size(); i++) {
        sim.top.dl_addr = (uint32_t)i;
        sim.top.dl_data = img[i];
        sim.top.dl_wr = 1;
        sim.tick();
        sim.top.dl_wr = 0;
        while (sim.top.dl_busy) sim.tick();
    }
    sim.top.dl_active = 0;
    // wrapper: reset = cart_download | dl_wait — nothing else holds it
    for (int i = 0; i < 4000000 && (sim.top.dl_active || sim.top.dl_wait); i++) {
        sim.tick();
    }
    sim.top.reset = 0;

    // the new cart (fb_pattern) must boot from DDR3: it sets dma_ctl and
    // draws; its vsync-NMI enable bit is the cheapest liveness signal
    uint64_t guard = sim.cycles + 3000000;
    while (!(sim.dmaCtl() & 0x04) && sim.cycles < guard) sim.tick();
    CHECK(sim.cartPresent(), "cart_present not set after OSD-style load");
    CHECK(sim.dmaCtl() & 0x04, "new cart never initialized dma_ctl (froze)");

    std::printf("PASS cart_osd_load: mid-run download resets into the new cart\n");
    return 0;
}

// Integration test for the M7 .gtr download path: the existing ram_pattern
// cart (32 KB → EEPROM32K) is streamed byte-by-byte through the core's
// download port — exactly what ioctl does on hardware — and must boot from
// the DDR3-backed cartridge (not the boot-cart bus) and leave its known
// pattern in system RAM. Same assertions as cart_ram_pattern, which runs
// the identical image over the external boot path.

#include "sim_harness.h"

int main() {
    Sim sim;

    FILE* f = std::fopen("../testroms/ram_pattern.gtr", "rb");
    if (!f) { std::perror("ram_pattern.gtr"); return 1; }
    std::vector<uint8_t> img(32768);
    size_t n = std::fread(img.data(), 1, img.size(), f);
    std::fclose(f);
    CHECK(n == img.size(), "short read: %zu", n);

    // poison the external boot path so a pass can only come from DDR3
    std::fill(sim.cart.begin(), sim.cart.end(), 0xFF);

    sim.gtrDownload(img);
    sim.reset();

    // generous budget: banked-window execution stalls once per 16 bytes
    for (int i = 0; i < 300000; i++) sim.tick();

    CHECK(sim.cartPresent(), "cart_present not set");
    CHECK(sim.sysram(0x1FFF) == 0xC3, "completion marker: %02x",
          sim.sysram(0x1FFF));

    for (int i = 0; i < 256; i++) {
        uint8_t want = (uint8_t)(i ^ 0x5A);
        CHECK(sim.sysram(0x0040 + i) == want, "fill[%02x] = %02x, want %02x",
              i, sim.sysram(0x0040 + i), want);
    }
    CHECK(sim.sysram(0x0000) == 0xB0, "bank0 marker: %02x", sim.sysram(0x0000));
    CHECK(sim.sysram(0x6000) == 0xB3, "bank3 marker: %02x", sim.sysram(0x6000));

    std::printf("PASS cart_download: .gtr streamed to DDR3, boots, pattern OK\n");
    return 0;
}

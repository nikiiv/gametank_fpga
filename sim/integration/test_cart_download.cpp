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

    // ---- second download onto the same core: a 16 KB cart ----------------
    // Non-32K EEPROM sizes exist in the wild (Mieyen.gtr is 16 KB) and map
    // as an end-aligned mirror = addr & (size-1) for power-of-two sizes.
    // File offset f is visible at both $8000+f and $C000+f.
    std::vector<uint8_t> img16(16384);
    for (uint32_t i = 0; i < img16.size(); i++)
        img16[i] = (uint8_t)(i ^ (i >> 7) ^ 0x33);
    auto put16 = [&](uint32_t off, std::initializer_list<uint8_t> b) {
        std::copy(b.begin(), b.end(), img16.begin() + off);
    };
    put16(0x100, {0x78, 0xA2, 0xFF, 0x9A,           // SEI LDX TXS
                  0x9C, 0x05, 0x20,                 // STZ $2005
                  0xAD, 0x23, 0x81, 0x85, 0x10,     // $8123 -> $10
                  0xAD, 0x00, 0xFF, 0x85, 0x11,     // $FF00 -> $11
                  0xAD, 0xFF, 0x9F, 0x85, 0x12,     // $9FFF -> $12
                  0xA9, 0xC3, 0x85, 0x1F,           // done marker
                  0x4C, 0x1A, 0xC1});               // halt: JMP $C11A
    put16(0x3FFA, {0x1A, 0xC1, 0x00, 0xC1, 0x1A, 0xC1});  // NMI RESET IRQ

    sim.gtrDownload(img16);
    sim.reset();
    uint64_t guard = sim.cycles + 2000000;
    while (sim.sysram(0x001F) != 0xC3 && sim.cycles < guard) sim.tick();
    CHECK(sim.sysram(0x001F) == 0xC3, "16K cart did not run: %02x",
          sim.sysram(0x001F));
    CHECK(sim.sysram(0x10) == img16[0x0123], "16K $8123: %02x want %02x",
          sim.sysram(0x10), img16[0x0123]);
    CHECK(sim.sysram(0x11) == img16[0x3F00], "16K $FF00: %02x want %02x",
          sim.sysram(0x11), img16[0x3F00]);
    CHECK(sim.sysram(0x12) == img16[0x1FFF], "16K $9FFF: %02x want %02x",
          sim.sysram(0x12), img16[0x1FFF]);

    std::printf("PASS cart_download: .gtr streamed to DDR3, boots, pattern OK; "
                "16K mirror + re-download OK\n");
    return 0;
}

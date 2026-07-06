// Unit test for the DDR3 cartridge controller (rtl/cart.sv), Flash2M mode.
//
// A synthetic 2 MB image is seeded into the DDR model (sparse download sets
// size/type and triggers the fixed-bank fill). Its fixed bank ($C000-$FFFF,
// image 0x1FC000+) holds a hand-assembled program that:
//   - programs the VIA Port A SPI (PA0 clk, PA1 data, PA2 latch) to load
//     the 74HC595 bank register with $01, $55, then $00
//   - reads banked-window bytes in each bank, plus fixed-bank bytes,
//     storing them to zero page
//   - sums 32 sequential banked bytes (exercises the prefetch path)
// The test asserts every stored byte against the known image pattern, the
// latched bank_mask (bit 7 forced high), and the checksum.

#include "sim_harness.h"

static uint8_t pat(uint32_t i) {
    return (uint8_t)((i & 0xFF) ^ ((i >> 8) & 0xFF) ^ ((i >> 16) * 0x5B));
}

int main() {
    Sim sim;

    std::vector<uint8_t> img(2 * 1024 * 1024);
    for (uint32_t i = 0; i < img.size(); i++) img[i] = pat(i);

    // ---- fixed-bank program (image 0x1FC000 = $C000) ----------------------
    const uint32_t FB = 0x1FC000;
    auto put = [&](uint32_t off, std::initializer_list<uint8_t> b) {
        std::copy(b.begin(), b.end(), img.begin() + FB + off);
    };
    // setbank at $C000: shift A into the 595 MSB-first, then latch
    put(0x000, {0x85, 0x00,            // STA $00
                0xA2, 0x08,            // LDX #8
                0x06, 0x00,            // sb: ASL $00
                0xA9, 0x00,            // LDA #0
                0x2A,                  // ROL A     (A = data bit)
                0x0A,                  // ASL A     (-> PA1)
                0x8D, 0x01, 0x28,      // STA $2801 (data, clk low)
                0x09, 0x01,            // ORA #1
                0x8D, 0x01, 0x28,      // STA $2801 (clk rise -> shift)
                0xCA,                  // DEX
                0xD0, 0xEF,            // BNE sb
                0x9C, 0x01, 0x28,      // STZ $2801 (clk low)
                0xA9, 0x04,            // LDA #4
                0x8D, 0x01, 0x28,      // STA $2801 (latch rise -> bank_mask)
                0x9C, 0x01, 0x28,      // STZ $2801
                0x60});                // RTS
    // main at $C030
    put(0x030, {0x78, 0xD8,            // SEI CLD
                0xA2, 0xFF, 0x9A,      // LDX #$FF TXS
                0x9C, 0x05, 0x20,      // STZ $2005
                0xA9, 0x07,            // LDA #7
                0x8D, 0x03, 0x28,      // STA $2803 (DDRA: PA0-2 out)
                0x9C, 0x01, 0x28,      // STZ $2801
                0xA9, 0x01,            // LDA #1
                0x20, 0x00, 0xC0,      // JSR setbank
                0xAD, 0x00, 0x80, 0x85, 0x10,   // $8000 -> $10
                0xAD, 0x34, 0x92, 0x85, 0x11,   // $9234 -> $11
                0xAD, 0xFF, 0xBF, 0x85, 0x12,   // $BFFF -> $12
                0xA9, 0x55,            // LDA #$55
                0x20, 0x00, 0xC0,      // JSR setbank
                0xAD, 0x00, 0x80, 0x85, 0x13,   // $8000 -> $13
                0xAD, 0xA5, 0xA5, 0x85, 0x14,   // $A5A5 -> $14
                0xA9, 0x00,            // LDA #0
                0x20, 0x00, 0xC0,      // JSR setbank
                0xAD, 0x01, 0x80, 0x85, 0x15,   // $8001 -> $15
                0xAD, 0x23, 0xC1, 0x85, 0x16,   // $C123 -> $16
                0xAD, 0xF0, 0xFF, 0x85, 0x17,   // $FFF0 -> $17
                0xA2, 0x00,            // LDX #0
                0xBD, 0x00, 0x80,      // cpy: LDA $8000,X
                0x95, 0x20,            // STA $20,X
                0xE8,                  // INX
                0xE0, 0x20,            // CPX #$20
                0xD0, 0xF6,            // BNE cpy
                0xA9, 0xC3, 0x85, 0x1F,         // done marker
                0x4C, 0x87, 0xC0});    // halt: JMP $C087
    // vectors
    put(0x3FFA, {0x87, 0xC0,           // NMI -> halt
                 0x30, 0xC0,           // RESET -> $C030
                 0x87, 0xC0});         // IRQ -> halt

    sim.gtrDownloadSparse(img);
    sim.reset();

    uint64_t guard = sim.cycles + 4000000;
    while (sim.sysram(0x001F) != 0xC3 && sim.cycles < guard) sim.tick();
    if (sim.sysram(0x001F) != 0xC3) {
        std::fprintf(stderr, "debug: present=%d bank_mask=%02x zp:",
                     sim.cartPresent(), sim.bankMask());
        for (int i = 0x10; i <= 0x1F; i++)
            std::fprintf(stderr, " %02x", sim.sysram(i));
        std::fprintf(stderr, "\n     ddr vec: %02x %02x  ddr $C030: %02x\n",
                     sim.ddr[Sim::CART_OFF + 0x1FFFFC],
                     sim.ddr[Sim::CART_OFF + 0x1FFFFD],
                     sim.ddr[Sim::CART_OFF + 0x1FC030]);
        auto& fr = sim.top.rootp->gametank__DOT__cart__DOT__fixedram;
        std::fprintf(stderr, "     fixedram[0]=%016llx [6]=%016llx [7FF]=%016llx\n",
                     (unsigned long long)fr[0], (unsigned long long)fr[6],
                     (unsigned long long)fr[0x7FF]);
        std::fprintf(stderr, "     stack: %02x %02x %02x\n",
                     sim.sysram(0x01FF), sim.sysram(0x01FE), sim.sysram(0x01FD));
    }
    CHECK(sim.sysram(0x001F) == 0xC3, "program did not complete");

    CHECK(sim.cartPresent(), "cart_present not set");
    CHECK(sim.bankMask() == 0x80, "bank_mask %02x, want 80 (bit7 forced)",
          sim.bankMask());

    auto ck = [&](uint16_t zp, uint32_t off, const char* what) {
        CHECK(sim.sysram(zp) == pat(off), "%s: %02x, want %02x (img %06x)",
              what, sim.sysram(zp), pat(off), off);
    };
    ck(0x10, 0x1u * 0x4000 + 0x0000, "bank1 $8000");
    ck(0x11, 0x1u * 0x4000 + 0x1234, "bank1 $9234");
    ck(0x12, 0x1u * 0x4000 + 0x3FFF, "bank1 $BFFF");
    ck(0x13, 0x55u * 0x4000 + 0x0000, "bank55 $8000");
    ck(0x14, 0x55u * 0x4000 + 0x25A5, "bank55 $A5A5");
    ck(0x15, 0x0u * 0x4000 + 0x0001, "bank0 $8001");
    ck(0x16, 0x1FC123, "fixed $C123");
    ck(0x17, 0x1FFFF0, "fixed $FFF0");

    // sequential run over bank0 $8000-$801F (prefetch path, byte-exact)
    for (uint32_t i = 0; i < 0x20; i++)
        CHECK(sim.sysram(0x20 + i) == pat(i),
              "seq[%02x]: %02x, want %02x", i, sim.sysram(0x20 + i), pat(i));

    std::printf("PASS cart: Flash2M banking, VIA SPI, fixed bank, seq refill\n");
    return 0;
}

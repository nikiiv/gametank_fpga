// CPU GRAM-window upload backpressure. Sprite sheets are written through the
// $4000 window with COPY_ENABLE=0, and real games can do this as a tight byte
// stream. The DDR-backed GRAM path has one pending CPU write slot, so a DDR
// busy stretch must stall RDY until that write command is accepted; otherwise
// later sprite bytes overwrite the pending slot and disappear.

#include "sim_harness.h"

static uint8_t pat(int i) { return (uint8_t)(i ^ 0x5A); }

int main() {
    Sim sim;

    auto put = [&](uint32_t off, std::initializer_list<uint8_t> b) {
        std::copy(b.begin(), b.end(), sim.cart.begin() + off);
    };

    // Boot from the harness cart, not the DDR cart, so the only intentional
    // DDR pressure is the forced busy interval below. Program:
    //   bank=0, COPY_ENABLE=0, then write 128 patterned bytes to GRAM $4000,X.
    put(0x0000, {0x78,                   // SEI
                 0xD8,                   // CLD
                 0xA2, 0xFF,             // LDX #$FF
                 0x9A,                   // TXS
                 0x9C, 0x05, 0x20,       // STZ $2005
                 0x9C, 0x07, 0x20,       // STZ $2007
                 0xA2, 0x00,             // LDX #0
                 0x8A,                   // loop: TXA
                 0x49, 0x5A,             // EOR #$5A
                 0x9D, 0x00, 0x40,       // STA $4000,X
                 0xE8,                   // INX
                 0xE0, 0x80,             // CPX #$80
                 0xD0, 0xF5,             // BNE loop
                 0xA9, 0xC3,             // LDA #$C3
                 0x85, 0x1F,             // STA $1F
                 0x4C, 0x1C, 0x80});     // halt: JMP halt
    put(0x7FFA, {0x1C, 0x80,             // NMI -> halt
                 0x00, 0x80,             // RESET -> $8000
                 0x1C, 0x80});           // IRQ -> halt

    sim.forceBusy = 60000;
    sim.reset();

    uint64_t guard = sim.cycles + 1000000;
    while (sim.sysram(0x001F) != 0xC3 && sim.cycles < guard) sim.tick();
    CHECK(sim.sysram(0x001F) == 0xC3, "upload cart did not finish");

    // Let any accepted pre-fix pending command drain, so the assertion
    // distinguishes one landed write from the full 128-byte upload.
    for (int i = 0; i < 100000; i++) sim.tick();

    int bad = 0;
    for (int i = 0; i < 128; i++) {
        uint8_t got = sim.ddr[(uint32_t)i];
        uint8_t want = pat(i);
        if (got != want && bad++ < 8)
            std::fprintf(stderr, "gram[%d]: %02x want %02x\n", i, got, want);
    }
    CHECK(bad == 0, "%d GRAM upload bytes lost under DDR backpressure", bad);

    std::printf("PASS gram_write_backpressure: tight GRAM upload survives "
                "DDR busy stretch\n");
    return 0;
}

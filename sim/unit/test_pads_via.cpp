// Directed test for the gamepad ports ($2008/$2009) and the 6522 VIA
// ($2800-$2FFF): scripted joystick input is injected at the core boundary
// and a hand-assembled cart reads both pads through their select phases,
// exercising the select-toggle and cross-reset FF behavior (hardware-true
// bit 7 = select state; bit 6 = unfitted extra button, reads 1), then does
// VIA register writes/readbacks (ORA/DDRA/DDRB — the register-file usage
// GameTank software actually makes of the VIA).

#include "sim_harness.h"

int main() {
    Sim sim;

    std::vector<uint8_t> prog = {
        0xAD, 0x08, 0x20, 0x85, 0x10,   // LDA $2008  STA $10  (sel 0)
        0xAD, 0x08, 0x20, 0x85, 0x11,   // LDA $2008  STA $11  (sel 1)
        0xAD, 0x08, 0x20, 0x85, 0x12,   // LDA $2008  STA $12  (sel 0)
        0xAD, 0x09, 0x20, 0x85, 0x13,   // LDA $2009  STA $13  (pad2 sel 0)
        0xAD, 0x09, 0x20, 0x85, 0x14,   // LDA $2009  STA $14  (pad2 sel 1)
        0xAD, 0x08, 0x20, 0x85, 0x15,   // LDA $2008  STA $15  (cross-reset -> sel 0)
        0xA9, 0xFF, 0x8D, 0x03, 0x28,   // LDA #$FF   STA $2803 (DDRA)
        0xA9, 0xA5, 0x8D, 0x01, 0x28,   // LDA #$A5   STA $2801 (ORA)
        0xAD, 0x01, 0x28, 0x85, 0x16,   // LDA $2801  STA $16
        0xAD, 0x03, 0x28, 0x85, 0x17,   // LDA $2803  STA $17
        0xA9, 0x3C, 0x8D, 0x02, 0x28,   // LDA #$3C   STA $2802 (DDRB)
        0xAD, 0x02, 0x28, 0x85, 0x18,   // LDA $2802  STA $18
        0xA9, 0xC3, 0x85, 0x1F,         // LDA #$C3   STA $1F  (done)
    };
    uint16_t trap = (uint16_t)(0x8000 + prog.size());
    prog.push_back(0x4C);
    prog.push_back(trap & 0xFF);
    prog.push_back(trap >> 8);

    std::copy(prog.begin(), prog.end(), sim.cart.begin());
    sim.cart[0x7FFC] = 0x00;
    sim.cart[0x7FFD] = 0x80;

    sim.reset();
    sim.top.joy1 = (1 << 4) | (1 << 3);   // A + Up
    sim.top.joy2 = (1 << 0) | (1 << 7);   // Right + Start
    for (int i = 0; i < 60000; i++) sim.tick();

    CHECK(sim.sysram(0x001F) == 0xC3, "done marker: %02x", sim.sysram(0x001F));

    // pad1, A+Up: sel0 -> D7=0 D6=1 Start=1 A=0 Up=0 Down=1 D1=D0=1 = $67
    //             sel1 -> D7=1 D6=1 C=1 B=1 Up=0 Down=1 Left=1 Right=1 = $F7
    CHECK(sim.sysram(0x10) == 0x67, "pad1 read1 (sel0): %02x", sim.sysram(0x10));
    CHECK(sim.sysram(0x11) == 0xF7, "pad1 read2 (sel1): %02x", sim.sysram(0x11));
    CHECK(sim.sysram(0x12) == 0x67, "pad1 read3 (sel0 again): %02x", sim.sysram(0x12));

    // pad2, Right+Start: sel0 -> Start=0 -> $5F; sel1 -> Right=0 -> $FE
    CHECK(sim.sysram(0x13) == 0x5F, "pad2 read1 (sel0): %02x", sim.sysram(0x13));
    CHECK(sim.sysram(0x14) == 0xFE, "pad2 read2 (sel1): %02x", sim.sysram(0x14));

    // pad2 was read since -> pad1's select FF was cross-reset to 0
    CHECK(sim.sysram(0x15) == 0x67, "pad1 after cross-reset: %02x", sim.sysram(0x15));

    // VIA register file
    CHECK(sim.sysram(0x16) == 0xA5, "VIA ORA readback: %02x", sim.sysram(0x16));
    CHECK(sim.sysram(0x17) == 0xFF, "VIA DDRA readback: %02x", sim.sysram(0x17));
    CHECK(sim.sysram(0x18) == 0x3C, "VIA DDRB readback: %02x", sim.sysram(0x18));

    std::printf("PASS pads_via: select FFs, cross-reset, bit7/bit6, VIA regs\n");
    return 0;
}

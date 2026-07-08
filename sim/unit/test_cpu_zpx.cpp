// Directed zero-page indexed tests for the vendored 65C02.
//
// Ganymede's Climb Race draw routine derives the blitter GX value through:
//   LDX #$03 / LDA $00,X       -> $30
// and a nearby ORA $18,X path.  This catches regressions in zero-page indexed
// addressing both at a steady CPU cadence and with arbitrary RDY stalls.

#include "cpu65_harness.h"

#define CHECK(cond, ...)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n     ", __FILE__,       \
                         __LINE__, #cond);                                \
            std::fprintf(stderr, __VA_ARGS__);                            \
            std::fprintf(stderr, "\n");                                   \
            std::exit(1);                                                 \
        }                                                                 \
    } while (0)

static const uint16_t ENTRY_PC = 0x0400;
static const uint16_t TRAP_PC  = 0x0420;

static void loadProgram(Cpu65& cpu) {
    std::memset(cpu.mem, 0xFF, sizeof cpu.mem);

    // Zero-page tables matching the values observed in the Climb Race trace.
    static const uint8_t zp0[]  = {0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70};
    static const uint8_t zp18[] = {0x00, 0x00, 0x30, 0x30};
    std::memcpy(cpu.mem + 0x0000, zp0, sizeof zp0);
    std::memcpy(cpu.mem + 0x0018, zp18, sizeof zp18);

    // 0400: A2 03     LDX #$03
    // 0402: A9 00     LDA #$00
    // 0404: 15 18     ORA $18,X
    // 0406: AA        TAX
    // 0407: 86 20     STX $20       ; expect $30
    // 0409: A2 03     LDX #$03
    // 040B: B5 00     LDA $00,X
    // 040D: 85 21     STA $21       ; expect $30
    // 040F: A2 FE     LDX #$FE
    // 0411: B5 04     LDA $04,X     ; zero-page wrap: ($04 + $fe) & $ff = $02
    // 0413: 85 22     STA $22       ; expect $20
    // 0415: 4C 20 04  JMP $0420
    // 0420: 4C 20 04  JMP $0420
    static const uint8_t prog[] = {
        0xA2, 0x03, 0xA9, 0x00, 0x15, 0x18, 0xAA, 0x86,
        0x20, 0xA2, 0x03, 0xB5, 0x00, 0x85, 0x21, 0xA2,
        0xFE, 0xB5, 0x04, 0x85, 0x22, 0x4C, 0x20, 0x04
    };
    std::memcpy(cpu.mem + ENTRY_PC, prog, sizeof prog);
    cpu.mem[TRAP_PC + 0] = 0x4C;
    cpu.mem[TRAP_PC + 1] = uint8_t(TRAP_PC & 0xFF);
    cpu.mem[TRAP_PC + 2] = uint8_t(TRAP_PC >> 8);

    cpu.mem[0xFFFC] = uint8_t(ENTRY_PC & 0xFF);
    cpu.mem[0xFFFD] = uint8_t(ENTRY_PC >> 8);
    cpu.reset();
}

int main() {
    Cpu65 cpu;
    loadProgram(cpu);

    uint16_t trap = cpu.runToTrap(2000);
    CHECK(trap == TRAP_PC, "wrong trap PC: got %04x", trap);
    CHECK(cpu.mem[0x20] == 0x30, "ORA $18,X / TAX produced %02x", cpu.mem[0x20]);
    CHECK(cpu.mem[0x21] == 0x30, "LDA $00,X produced %02x", cpu.mem[0x21]);
    CHECK(cpu.mem[0x22] == 0x20, "zero-page wrap LDA produced %02x", cpu.mem[0x22]);

    std::printf("PASS cpu_zpx\n");
    return 0;
}

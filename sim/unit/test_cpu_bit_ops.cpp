// Directed tests for Rockwell/WDC 65C02 bit operations used by SDK output:
//   RMB/SMB zp       clear/set a zero-page bit, flags unchanged
//   BBR/BBS zp,rel   branch on zero-page bit reset/set

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
static const uint16_t TRAP_PC  = 0x0460;

static void loadProgram(Cpu65& cpu) {
    std::memset(cpu.mem, 0xFF, sizeof cpu.mem);

    // 0400: A9 2B     LDA #$2B
    // 0402: 85 1C     STA $1C
    // 0404: 6F 1C 02  BBR6 $1C,+2   ; taken, skips LDA #$EE
    // 0407: A9 EE     LDA #$EE
    // 0409: 85 20     STA $20       ; expect $2B
    //
    // 040B: A9 40     LDA #$40
    // 040D: 85 1C     STA $1C
    // 040F: 6F 1C 02  BBR6 $1C,+2   ; not taken
    // 0412: A9 55     LDA #$55
    // 0414: 85 21     STA $21       ; expect $55
    //
    // 0416: A9 40     LDA #$40
    // 0418: 85 1C     STA $1C
    // 041A: A9 11     LDA #$11
    // 041C: EF 1C 02  BBS6 $1C,+2   ; taken, skips LDA #$EE
    // 041F: A9 EE     LDA #$EE
    // 0421: 85 22     STA $22       ; expect $11
    //
    // 0423: A9 00     LDA #$00
    // 0425: 85 1C     STA $1C
    // 0427: A9 22     LDA #$22
    // 0429: EF 1C 02  BBS6 $1C,+2   ; not taken
    // 042C: A9 66     LDA #$66
    // 042E: 85 23     STA $23       ; expect $66
    //
    // 0430: A9 FF     LDA #$FF
    // 0432: 85 30     STA $30
    // 0434: 37 30     RMB3 $30
    // 0436: A5 30     LDA $30
    // 0438: 85 24     STA $24       ; expect $F7
    //
    // 043A: A9 00     LDA #$00
    // 043C: 85 31     STA $31
    // 043E: B7 31     SMB3 $31
    // 0440: A5 31     LDA $31
    // 0442: 85 25     STA $25       ; expect $08
    // 0444: 08        PHP
    // 0445: 68        PLA
    // 0446: 85 26     STA $26       ; flags after LDA $31, not changed by SMB
    // 0448: 4C 60 04  JMP $0460
    // 0460: 4C 60 04  JMP $0460
    static const uint8_t prog[] = {
        0xA9, 0x2B, 0x85, 0x1C, 0x6F, 0x1C, 0x02, 0xA9,
        0xEE, 0x85, 0x20, 0xA9, 0x40, 0x85, 0x1C, 0x6F,
        0x1C, 0x02, 0xA9, 0x55, 0x85, 0x21, 0xA9, 0x40,
        0x85, 0x1C, 0xA9, 0x11, 0xEF, 0x1C, 0x02, 0xA9,
        0xEE, 0x85, 0x22, 0xA9, 0x00, 0x85, 0x1C, 0xA9,
        0x22, 0xEF, 0x1C, 0x02, 0xA9, 0x66, 0x85, 0x23,
        0xA9, 0xFF, 0x85, 0x30, 0x37, 0x30, 0xA5, 0x30,
        0x85, 0x24, 0xA9, 0x00, 0x85, 0x31, 0xB7, 0x31,
        0xA5, 0x31, 0x85, 0x25, 0x08, 0x68, 0x85, 0x26,
        0x4C, 0x60, 0x04
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

    uint16_t trap = cpu.runToTrap(4000);
    CHECK(trap == TRAP_PC, "wrong trap PC: got %04x", trap);
    CHECK(cpu.mem[0x20] == 0x2B, "BBR6 clear-bit branch got %02x", cpu.mem[0x20]);
    CHECK(cpu.mem[0x21] == 0x55, "BBR6 set-bit fallthrough got %02x", cpu.mem[0x21]);
    CHECK(cpu.mem[0x22] == 0x11, "BBS6 set-bit branch got %02x", cpu.mem[0x22]);
    CHECK(cpu.mem[0x23] == 0x66, "BBS6 clear-bit fallthrough got %02x", cpu.mem[0x23]);
    CHECK(cpu.mem[0x24] == 0xF7, "RMB3 result got %02x", cpu.mem[0x24]);
    CHECK(cpu.mem[0x25] == 0x08, "SMB3 result got %02x", cpu.mem[0x25]);
    CHECK((cpu.mem[0x26] & 0x82) == 0x00,
          "SMB unexpectedly changed N/Z flags: P=%02x", cpu.mem[0x26]);

    std::printf("PASS cpu_bit_ops\n");
    return 0;
}

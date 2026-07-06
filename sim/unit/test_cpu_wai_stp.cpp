// Directed tests for our WAI/STP patch on the vendored 65C02
// (docs/DEPENDENCIES.md §CPU). W65C02S semantics under test:
//
//   WAI, I=1: IRQ wakes the CPU; execution continues after WAI, no vector.
//   WAI, I=0: IRQ wakes the CPU and vectors; ISR runs, RTI resumes after WAI.
//   WAI:      NMI wakes the CPU and vectors regardless of I.
//   STP:      CPU halts; IRQ/NMI do not revive it; reset does.
//
// The test program is hand-assembled (a handful of bytes; no toolchain
// dependency). Zero-page mailboxes record progress:
//   $10 - main-line progress marker
//   $11 - IRQ handler entry count
//   $12 - NMI handler entry count

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

// Program at $0400 (entry via reset vector), trap at trapPC.
//   0400: 78        SEI            (or 58 CLI, patched per scenario)
//   0401: A9 00     LDA #$00
//   0403: 85 10     STA $10
//   0405: CB        WAI            (or DB STP)
//   0406: E6 10     INC $10
//   0408: 4C 08 04  JMP $0408      <- trap
// IRQ handler at $0500: E6 11 (INC $11), 40 (RTI)
// NMI handler at $0510: E6 12 (INC $12), 40 (RTI)
static void loadProgram(Cpu65& cpu, uint8_t iFlagOp, uint8_t waiOrStp) {
    static const uint8_t prog[] = {0x78, 0xA9, 0x00, 0x85, 0x10, 0xCB,
                                   0xE6, 0x10, 0x4C, 0x08, 0x04};
    std::memset(cpu.mem, 0xFF, 65536);
    std::memcpy(cpu.mem + 0x0400, prog, sizeof prog);
    cpu.mem[0x0400] = iFlagOp;             // SEI ($78) or CLI ($58)
    cpu.mem[0x0405] = waiOrStp;            // WAI ($CB) or STP ($DB)
    cpu.mem[0x0500] = 0xE6; cpu.mem[0x0501] = 0x11; cpu.mem[0x0502] = 0x40;
    cpu.mem[0x0510] = 0xE6; cpu.mem[0x0511] = 0x12; cpu.mem[0x0512] = 0x40;
    cpu.mem[0xFFFA] = 0x10; cpu.mem[0xFFFB] = 0x05;  // NMI
    cpu.mem[0xFFFC] = 0x00; cpu.mem[0xFFFD] = 0x04;  // RESET
    cpu.mem[0xFFFE] = 0x00; cpu.mem[0xFFFF] = 0x05;  // IRQ/BRK
    cpu.mem[0x10] = cpu.mem[0x11] = cpu.mem[0x12] = 0xEE;
    cpu.reset();
}

static void run(Cpu65& cpu, int n) { while (n--) cpu.tick(); }

static const uint16_t TRAP_PC = 0x0408;

int main() {
    Cpu65 cpu;

    // --- WAI with I=1: IRQ wakes without vectoring ---------------------
    loadProgram(cpu, 0x78 /*SEI*/, 0xCB /*WAI*/);
    run(cpu, 200);
    CHECK(cpu.mem[0x10] == 0x00 && !cpu.trapped(),
          "CPU not parked in WAI: $10=%02x", cpu.mem[0x10]);
    cpu.top.IRQ = 1;
    run(cpu, 40);
    cpu.top.IRQ = 0;
    CHECK(cpu.runToTrap(1000) == TRAP_PC, "wrong trap");
    CHECK(cpu.mem[0x10] == 0x01, "did not resume after WAI: $10=%02x",
          cpu.mem[0x10]);
    CHECK(cpu.mem[0x11] == 0xEE, "vectored despite I=1: $11=%02x",
          cpu.mem[0x11]);
    std::printf("PASS wai_i1: IRQ wake, no vector\n");

    // --- WAI with I=0: IRQ wakes and vectors ---------------------------
    loadProgram(cpu, 0x58 /*CLI*/, 0xCB /*WAI*/);
    run(cpu, 200);
    CHECK(cpu.mem[0x10] == 0x00 && !cpu.trapped(), "not parked in WAI");
    cpu.top.IRQ = 1;
    run(cpu, 6);       // long enough to wake and vector, short enough that
    cpu.top.IRQ = 0;   // the ISR (which restores I=0) runs exactly once
    CHECK(cpu.runToTrap(1000) == TRAP_PC, "wrong trap");
    CHECK(cpu.mem[0x11] == 0xEF, "ISR did not run once: $11=%02x",
          cpu.mem[0x11]);
    CHECK(cpu.mem[0x10] == 0x01, "did not resume after ISR: $10=%02x",
          cpu.mem[0x10]);
    std::printf("PASS wai_i0: IRQ wake, vectored, resumed\n");

    // --- WAI + NMI: wakes and vectors regardless of I -------------------
    loadProgram(cpu, 0x78 /*SEI*/, 0xCB /*WAI*/);
    run(cpu, 200);
    CHECK(cpu.mem[0x10] == 0x00 && !cpu.trapped(), "not parked in WAI");
    cpu.top.NMI = 1;
    run(cpu, 40);
    cpu.top.NMI = 0;
    CHECK(cpu.runToTrap(1000) == TRAP_PC, "wrong trap");
    CHECK(cpu.mem[0x12] == 0xEF, "NMI handler did not run: $12=%02x",
          cpu.mem[0x12]);
    CHECK(cpu.mem[0x10] == 0x01, "did not resume after NMI: $10=%02x",
          cpu.mem[0x10]);
    std::printf("PASS wai_nmi: NMI wake, vectored, resumed\n");

    // --- STP: halted for good; only reset revives -----------------------
    loadProgram(cpu, 0x78 /*SEI*/, 0xDB /*STP*/);
    run(cpu, 200);
    CHECK(cpu.mem[0x10] == 0x00, "STP not reached cleanly");
    cpu.top.IRQ = 1;
    cpu.top.NMI = 1;
    run(cpu, 500);
    cpu.top.IRQ = 0;
    cpu.top.NMI = 0;
    CHECK(cpu.mem[0x10] == 0x00 && cpu.mem[0x11] == 0xEE && cpu.mem[0x12] == 0xEE,
          "STP not halted: $10=%02x $11=%02x $12=%02x",
          cpu.mem[0x10], cpu.mem[0x11], cpu.mem[0x12]);
    cpu.mem[0x10] = 0x55;  // program must overwrite this to prove it restarted
    cpu.reset();
    run(cpu, 200);
    CHECK(cpu.mem[0x10] == 0x00, "no restart after reset: $10=%02x",
          cpu.mem[0x10]);
    std::printf("PASS stp: halted through IRQ+NMI, reset revives\n");

    std::printf("PASS cpu_wai_stp\n");
    return 0;
}

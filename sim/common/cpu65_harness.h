// CPU-only harness for the vendored 65C02 core (rtl/cpu/cpu_65c02.v).
//
// Flat 64 KB synchronous RAM, the way Arlet's core expects it: DI presents
// the value addressed by AB in the *previous* cycle. Used by the Klaus
// import gate and the directed WAI/STP tests. IRQ/NMI are plain members so
// tests can drive interrupt scenarios.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <verilated.h>
#include "Vcpu_65c02.h"

class Cpu65 {
public:
    Vcpu_65c02 top;
    uint8_t    mem[65536];
    uint8_t    nextDI = 0xFF;  // synchronous-RAM pipeline register
    uint64_t   cycles = 0;

    // Trap (PC-loop) detection over SYNC opcode fetches.
    uint16_t lastSync      = 0xFFFF;
    int      sameSyncCount = 0;
    uint16_t syncPC        = 0xFFFF;  // most recent opcode fetch address

    Cpu65() { std::memset(mem, 0xFF, sizeof mem); }

    void loadImage(const std::string& path, uint16_t base) {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) { std::perror(path.c_str()); std::exit(1); }
        size_t n = std::fread(mem + base, 1, sizeof(mem) - base, f);
        std::fclose(f);
        if (n == 0) { std::fprintf(stderr, "empty image %s\n", path.c_str()); std::exit(1); }
    }

    void reset() {
        top.RDY = 1;
        top.IRQ = 0;
        top.NMI = 0;
        nextDI  = 0xFF;
        top.reset = 1;
        for (int i = 0; i < 4; i++) tick();
        top.reset = 0;
        lastSync = 0xFFFF;
        sameSyncCount = 0;
    }

    // One clk cycle with synchronous-RAM semantics: the address driven during
    // cycle N produces data on DI during cycle N+1, and consumers at the
    // posedge see the previous DI (nonblocking `DI <= mem[AB]` semantics).
    // Read-during-write returns old data, like inferred BRAM.
    void tick() {
        top.clk = 1;
        top.eval();        // posedge: CPU registers see last cycle's DI

        top.DI = nextDI;   // new data becomes visible after the edge
        top.eval();        // settle combinational AB/WE/DO for this cycle

        uint16_t ab = top.AB;
        nextDI = mem[ab];
        if (top.WE) mem[ab] = top.DO;

        if (top.SYNC) {
            if (ab == lastSync) sameSyncCount++;
            else { lastSync = ab; sameSyncCount = 0; }
            syncPC = ab;
        }

        top.clk = 0;
        top.eval();
        cycles++;
    }

    bool trapped() const { return sameSyncCount >= 8; }

    // Run until the CPU loops on one PC; returns the trap's opcode address.
    // (During the SYNC/DECODE cycle AB = opcode address + 1, hence the -1.)
    uint16_t runToTrap(uint64_t maxCycles) {
        uint64_t limit = cycles + maxCycles;
        while (!trapped()) {
            if (cycles >= limit) {
                std::fprintf(stderr,
                             "FAIL: no trap within %llu cycles (last SYNC %04x)\n",
                             (unsigned long long)maxCycles, syncPC);
                std::exit(1);
            }
            tick();
        }
        return lastSync - 1;
    }
};

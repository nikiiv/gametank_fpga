// Klaus Dormann functional-test import gate for the vendored 65C02 core.
//
// Usage: test_cpu_klaus <64KB-image> <success-pc-hex>
//
// Loads the image at $0000, points the reset vector at the conventional
// $0400 entry, runs until the CPU traps (PC self-loop), and passes iff the
// trap address is the test's success trap. On failure, prints the trap PC
// and the current Klaus test-case number ($0200) for diagnosis.

#include "cpu65_harness.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <image.bin> <success-pc-hex>\n", argv[0]);
        return 2;
    }
    const std::string image = argv[1];
    const uint16_t successPC = (uint16_t)std::strtoul(argv[2], nullptr, 16);

    Cpu65 cpu;
    cpu.loadImage(image, 0x0000);
    cpu.mem[0xFFFC] = 0x00;  // reset vector -> $0400 (Klaus convention)
    cpu.mem[0xFFFD] = 0x04;
    cpu.reset();

    // KLAUS_TRACE=N: print the first N cycles raw, then exit (debug aid).
    if (const char* tr = std::getenv("KLAUS_TRACE")) {
        long n = std::strtol(tr, nullptr, 10);
        for (long i = 0; i < n; i++) {
            cpu.tick();
            std::printf("cyc %3ld AB=%04x DInext=%02x WE=%d DO=%02x SYNC=%d\n",
                        i, cpu.top.AB, cpu.top.DI, cpu.top.WE, cpu.top.DO,
                        (int)cpu.top.SYNC);
        }
        return 0;
    }

    uint16_t trap = cpu.runToTrap(400'000'000ull);

    if (trap != successPC) {
        std::fprintf(stderr,
                     "FAIL %s: trapped at %04x (want %04x), test case %02x, "
                     "%llu cycles\n",
                     image.c_str(), trap, successPC, cpu.mem[0x0200],
                     (unsigned long long)cpu.cycles);
        return 1;
    }
    std::printf("PASS klaus %s: success trap %04x after %llu cycles\n",
                image.c_str(), trap, (unsigned long long)cpu.cycles);
    return 0;
}

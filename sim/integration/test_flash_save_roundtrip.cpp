// M9.1 flash-save persistence round trip. A game programs a byte into
// flash and issues the `$90` save command (the SDK / emulator save idiom);
// savectl must stream the modified 2 MB image out to the mounted .sav.
// After a full power cycle — fresh Sim, re-download the *original* image,
// re-mount that .sav — the restore path must overlay the save so the
// programmed byte reads back modified.
//
// This is the sim proxy for the M9.1 hardware gate (Ganymede's main game
// saves through $90 and its save survives a core reload).

#include "sim_harness.h"

// program at image offset 0 (bank 0 window $8000): AND-program 0x5A over
// the erased 0xFF, then $90 to persist.
static const uint32_t FB = 0x1FC000;                 // fixed bank -> $C000
static const uint8_t PROG[] = {
    0x78, 0xD8, 0xA2, 0xFF, 0x9A,                    // SEI CLD LDX#FF TXS
    0xA9, 0xA0, 0x8D, 0x00, 0x80,                    // LDA #$A0  STA $8000 (arm program)
    0xA9, 0x5A, 0x8D, 0x00, 0x80,                    // LDA #$5A  STA $8000 (program byte0)
    0xA9, 0x90, 0x8D, 0x00, 0x80,                    // LDA #$90  STA $8000 (save trigger)
    0xA9, 0xC3, 0x85, 0x1F,                          // LDA #$C3  STA $1F  (done marker)
    0x4C, 0x18, 0xC3                                 // halt: JMP $C318
};

static std::vector<uint8_t> makeImage() {
    std::vector<uint8_t> img(2 * 1024 * 1024, 0xFF);
    std::copy(PROG, PROG + sizeof(PROG), img.begin() + FB + 0x300);
    img[FB + 0x3FFC] = 0x00; img[FB + 0x3FFD] = 0xC3;   // RESET -> $C300
    img[FB + 0x3FFA] = 0x18; img[FB + 0x3FFB] = 0xC3;   // NMI  -> halt
    img[FB + 0x3FFE] = 0x18; img[FB + 0x3FFF] = 0xC3;   // IRQ  -> halt
    img[0x000000] = 0xFF;                               // program target (erased)
    return img;
}

int main() {
    std::vector<uint8_t> img = makeImage();
    std::vector<uint8_t> expected = img;
    expected[0] = 0x5A;

    // ---- phase 1: program flash + $90, capture the save file ----
    std::vector<uint8_t> saved;
    {
        Sim sim;
        sim.gtrDownloadSparse(img);
        sim.mountSave();                 // empty, writable -> savable
        sim.reset();

        uint64_t guard = sim.cycles + 40000000ull;
        while (sim.sysram(0x1F) != 0xC3 && sim.cycles < guard) sim.tick();
        CHECK(sim.sysram(0x1F) == 0xC3, "program did not finish");
        CHECK(sim.ddr[Sim::CART_OFF + 0] == 0x5A,
              "flash program did not land in DDR: %02x", sim.ddr[Sim::CART_OFF]);

        // let the save FSM start and run to completion
        guard = sim.cycles + 60000000ull;
        while (sim.saveState() == 0 && sim.cycles < guard) sim.tick();
        CHECK(sim.saveState() != 0, "save never started after $90");
        while (sim.saveState() != 0 && sim.cycles < guard) sim.tick();
        CHECK(sim.saveState() == 0, "save did not complete");

        saved = sim.savefile;
        CHECK(saved.size() == 2u * 1024 * 1024, "save file wrong size: %zu",
              saved.size());
        CHECK(saved[0] == 0x5A, "programmed byte not in save file: %02x",
              saved[0]);
        CHECK(saved[FB + 0x300] == PROG[0], "code region not saved");

        size_t mismatch = 0;
        while (mismatch < expected.size() && saved[mismatch] == expected[mismatch])
            ++mismatch;
        CHECK(mismatch == expected.size(),
              "save image differs at %06zx: got %02x expected %02x",
              mismatch,
              mismatch < saved.size() ? saved[mismatch] : 0,
              mismatch < expected.size() ? expected[mismatch] : 0);
    }

    // ---- phase 2: power cycle — original image, mount the save, restore ----
    {
        Sim sim;
        sim.gtrDownloadSparse(img);                 // pristine: byte0 = 0xFF
        CHECK(sim.ddr[Sim::CART_OFF + 0] == 0xFF, "fresh image not pristine");

        sim.mountSave(saved);                       // non-empty -> auto restore
        // The restore port must remain selected through its final queued
        // write. Make the idle ioctl pins hostile so a premature handoff is
        // visible instead of accidentally replaying the correct last byte.
        sim.top.dl_addr = 0x012345;
        sim.top.dl_data = 0x00;
        uint64_t guard = sim.cycles + 60000000ull;
        while (sim.saveState() == 0 && sim.cycles < guard) sim.tick();
        CHECK(sim.saveState() != 0, "restore never started after mount");
        while (sim.saveState() != 0 && sim.cycles < guard) sim.tick();
        CHECK(sim.saveState() == 0, "restore did not complete");

        // wait for the cart to re-present after the restore's dl_end fill
        guard = sim.cycles + 400000;
        while (!sim.cartPresent() && sim.cycles < guard) sim.tick();
        CHECK(sim.cartPresent(), "cart not present after restore");

        CHECK(sim.ddr[Sim::CART_OFF + 0] == 0x5A,
              "restore did not overlay the save: %02x",
              sim.ddr[Sim::CART_OFF + 0]);

        size_t mismatch = 0;
        while (mismatch < expected.size() &&
               sim.ddr[Sim::CART_OFF + mismatch] == expected[mismatch])
            ++mismatch;
        CHECK(mismatch == expected.size(),
              "restored image differs at %06zx: got %02x expected %02x",
              mismatch,
              mismatch < expected.size()
                  ? sim.ddr[Sim::CART_OFF + mismatch] : 0,
              mismatch < expected.size() ? expected[mismatch] : 0);
    }

    std::printf("PASS flash_save_roundtrip: $90 save + power-cycle restore\n");
    return 0;
}

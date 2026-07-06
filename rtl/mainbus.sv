//============================================================================
//  GameTank main CPU bus: W65C02S + address decode + banked system RAM.
//
//  The CPU runs on clk_sys with RDY as a 3.579545 MHz clock-enable (1-in-8
//  strobe). Bus timing with an RDY-gated Arlet core is subtle: between
//  strobes the core's DIMUX shows the *previous* cycle's data (DIHOLD), so
//  DIMUX-derived addresses (vector fetches, zp/abs store targets) are only
//  valid during the strobe cycle itself. Therefore every bus transaction —
//  reads, writes, register writes — latches at the cpu_ce strobe using the
//  strobe-cycle AB/WE/DO, and read data is presented by the next strobe
//  (a full 8-clock window; mirrors the 1-cycle synchronous-RAM contract
//  the core is designed for).
//
//  Memory map implemented here (docs/HARDWARE.md):
//    $0000-$1FFF  system RAM window; physical = {banking[7:6], addr[12:0]}
//                 (4 x 8 KB banks into one 32 KB BRAM)
//    $2005        banking register (writes; [7:6] = RAM bank used here,
//                 [5:0] latched for the blitter/video milestones)
//    $8000-$FFFF  cartridge window (abstract bus; sim-backed until M7 DDR3)
//    elsewhere    open bus (last value seen on the data bus) — audio RAM,
//                 VIA, pads and the VDMA window arrive in M3-M6
//============================================================================

module mainbus
(
    input  logic        clk_sys,
    input  logic        reset,
    input  logic        cpu_ce,      // 3.579545 MHz strobe: CPU RDY

    output logic [14:0] cart_addr,   // $8000-$FFFF window (strobe-latched)
    input  logic [7:0]  cart_data,   // must be valid within the 8-clk window

    input  logic        irq,         // blitter IRQ (M4), VIA (M5)
    input  logic        nmi          // vsync NMI (M3/M4)
);

wire [15:0] cpu_ab;
wire  [7:0] cpu_do;
logic [7:0] cpu_di;
wire        cpu_we;

/* verilator lint_off PINCONNECTEMPTY */
cpu_65c02 cpu
(
    .clk   (clk_sys),
    .reset (reset),
    .AB    (cpu_ab),
    .DI    (cpu_di),
    .DO    (cpu_do),
    .WE    (cpu_we),
    .IRQ   (irq),
    .NMI   (nmi),
    .RDY   (cpu_ce),
    .SYNC  ()
);
/* verilator lint_on PINCONNECTEMPTY */

// Banking register $2005 (writes decoded as addr&7 within $2000-$2007)
logic [7:0] banking /*verilator public_flat_rd*/;

wire reg_write = cpu_we && (cpu_ab[15:3] == 13'h400);  // $2000-$2007

always_ff @(posedge clk_sys) begin
    if (reset)
        banking <= '0;
    else if (cpu_ce && reg_write && cpu_ab[2:0] == 3'd5)
        banking <= cpu_do;
end

// System RAM: 32 KB BRAM, CPU sees an 8 KB window at $0000 selected by
// banking[7:6]. Read and write both latch at the strobe; read data is
// stable one clock later and holds through the next window.
logic [7:0] sysram [0:32767] /*verilator public_flat_rd*/;

wire        ram_sel  = (cpu_ab[15:13] == 3'b000);
wire [14:0] ram_addr = {banking[7:6], cpu_ab[12:0]};
logic [7:0] ram_q;

always_ff @(posedge clk_sys) begin
    if (cpu_ce) begin
        if (ram_sel && cpu_we)
            sysram[ram_addr] <= cpu_do;
        ram_q <= sysram[ram_addr];
    end
end

// Read-source select, latched at the strobe alongside the address so the
// data mux below is loop-free (the core's AB is a function of DI on
// vector/jump-target cycles).
typedef enum logic [1:0] { SEL_RAM, SEL_CART, SEL_OPEN } rdsel_e;
rdsel_e     rd_sel;
logic [7:0] open_bus;

always_ff @(posedge clk_sys) begin
    if (cpu_ce) begin
        unique casez (cpu_ab)
            16'b000?_????_????_????: rd_sel <= SEL_RAM;   // $0000-$1FFF
            16'b1???_????_????_????: rd_sel <= SEL_CART;  // $8000-$FFFF
            default:                 rd_sel <= SEL_OPEN;
        endcase
        cart_addr <= cpu_ab[14:0];
        open_bus  <= cpu_we ? cpu_do : cpu_di;  // last byte on the data bus
    end
end

always_comb begin
    unique case (rd_sel)
        SEL_RAM:  cpu_di = ram_q;
        SEL_CART: cpu_di = cart_data;
        default:  cpu_di = open_bus;
    endcase
end

endmodule

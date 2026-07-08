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
    output logic        cart_rd,     // 1-clk pulse: read latched this strobe
    output logic        cart_wr,     // 1-clk pulse: write latched this strobe
    output logic [7:0]  cart_wdata,  // write data (latched with cart_addr)
    input  logic [7:0]  cart_data,   // must be valid within the 8-clk window

    // VDMA window ($4000-$7FFF): shared strobe-latched address/data with
    // per-target write pulses. Routing (docs/HARDWARE.md §Blitter):
    //   COPY_ENABLE=1              -> blitter parameters (reads: open bus)
    //   COPY_ENABLE=0, CPU_TO_VRAM -> framebuffer (vram port A)
    //   COPY_ENABLE=0, else        -> GRAM (quadrant from blitter gram_mid)
    output logic [13:0] win_addr,
    output logic [7:0]  win_din,
    output logic        vram_we,
    output logic        gram_we,
    output logic        gram_rd,     // pulses when a GRAM window read latches
    output logic        blit_param_we,
    input  logic [7:0]  vram_dout,
    input  logic [7:0]  gram_dout,

    output logic [7:0]  dma_ctl,     // $2007 register (docs/HARDWARE.md)
    output logic [7:0]  bank_reg,    // $2005 register

    // gamepad ports $2008/$2009 (reads only; side effects in rtl/pads.sv)
    output logic        pad1_rd,
    output logic        pad2_rd,
    input  logic [7:0]  pad_q,

    // 6522 VIA at $2800-$2FFF (addr & 15)
    output logic        via_wen,
    output logic        via_ren,
    input  logic [7:0]  via_q,

    // audio: registers in $2000-$2007 (slots 0/1/6) + $3000-$3FFF RAM window
    output logic        acp_reset_we,
    output logic        acp_nmi_we,
    output logic        acp_rate_we,
    output logic        aram_we,
    input  logic [7:0]  aram_q,

    input  logic        irq,         // blitter IRQ (gated), VIA (M5)
    input  logic        nmi          // vsync NMI (from scanout, gated dma_ctl[2])
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

// System registers $2000-$2007 (writes decoded as addr&7):
//   $2005 banking, $2007 DMA control (other slots are audio regs, M6)
logic [7:0] banking /*verilator public_flat_rd*/;

wire reg_write = cpu_we && (cpu_ab[15:3] == 13'h400);  // $2000-$2007

always_ff @(posedge clk_sys) begin
    if (reset) begin
        banking <= '0;
        dma_ctl <= '0;
    end
    else if (cpu_ce && reg_write) begin
        if (cpu_ab[2:0] == 3'd5) banking <= cpu_do;
        if (cpu_ab[2:0] == 3'd7) dma_ctl <= cpu_do;
    end
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

// VDMA window $4000-$7FFF. Address/data latch at the strobe; write pulses
// land one clock later, aligned with the latched values (cpu_do changes
// right after the strobe posedge, so it must be captured, not passed
// through).
wire vdma_sel  = (cpu_ab[15:14] == 2'b01);
wire vram_sel  = vdma_sel && !dma_ctl[0] &&  dma_ctl[5];
wire gram_sel  = vdma_sel && !dma_ctl[0] && !dma_ctl[5];
wire param_sel = vdma_sel &&  dma_ctl[0];
wire via_sel   = (cpu_ab[15:11] == 5'b00101);   // $2800-$2FFF
wire aram_sel  = (cpu_ab[15:12] == 4'b0011);    // $3000-$3FFF

assign bank_reg = banking;

always_ff @(posedge clk_sys) begin
    if (cpu_ce) begin
        win_addr <= cpu_ab[13:0];
        win_din  <= cpu_do;
    end
    vram_we       <= cpu_ce && vram_sel  && cpu_we;
    gram_we       <= cpu_ce && gram_sel  && cpu_we;
    gram_rd       <= cpu_ce && gram_sel  && !cpu_we;
    blit_param_we <= cpu_ce && param_sel && cpu_we;
    pad1_rd       <= cpu_ce && (cpu_ab == 16'h2008) && !cpu_we;
    pad2_rd       <= cpu_ce && (cpu_ab == 16'h2009) && !cpu_we;
    // The VIA samples wen/ren at its phi2 edges (the strobes), so these are
    // held as levels for the whole following window; the access commits at
    // the next strobe (one CPU cycle late — invisible to the register-file
    // usage that GameTank software makes of the VIA, see DEPENDENCIES.md).
    if (cpu_ce) begin
        via_wen <= via_sel && cpu_we;
        via_ren <= via_sel && !cpu_we;
    end
    acp_reset_we <= cpu_ce && reg_write && cpu_ab[2:0] == 3'd0;
    acp_nmi_we   <= cpu_ce && reg_write && cpu_ab[2:0] == 3'd1;
    acp_rate_we  <= cpu_ce && reg_write && cpu_ab[2:0] == 3'd6;
    aram_we      <= cpu_ce && aram_sel && cpu_we;
    cart_rd      <= cpu_ce && cpu_ab[15] && !cpu_we;
    cart_wr      <= cpu_ce && cpu_ab[15] && cpu_we;
    if (cpu_ce) cart_wdata <= cpu_do;
end

// Read-source select, latched at the strobe alongside the address so the
// data mux below is loop-free (the core's AB is a function of DI on
// vector/jump-target cycles).
typedef enum logic [3:0] { SEL_RAM, SEL_CART, SEL_VRAM, SEL_GRAM,
                           SEL_PAD, SEL_VIA, SEL_ARAM, SEL_OPEN } rdsel_e;
rdsel_e     rd_sel;
logic [7:0] open_bus;

always_ff @(posedge clk_sys) begin
    if (cpu_ce) begin
        unique casez (cpu_ab)
            16'b000?_????_????_????: rd_sel <= SEL_RAM;   // $0000-$1FFF
            16'b0010_0000_0000_100?: rd_sel <= SEL_PAD;   // $2008/$2009
            16'b0010_1???_????_????: rd_sel <= SEL_VIA;   // $2800-$2FFF
            16'b0011_????_????_????: rd_sel <= SEL_ARAM;  // $3000-$3FFF
            16'b01??_????_????_????: rd_sel <= vram_sel ? SEL_VRAM :
                                               gram_sel ? SEL_GRAM : SEL_OPEN;
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
        SEL_VRAM: cpu_di = vram_dout;
        SEL_GRAM: cpu_di = gram_dout;
        SEL_PAD:  cpu_di = pad_q;
        SEL_VIA:  cpu_di = via_q;
        SEL_ARAM: cpu_di = aram_q;
        default:  cpu_di = open_bus;
    endcase
end

endmodule

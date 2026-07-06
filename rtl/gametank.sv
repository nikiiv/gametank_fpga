//============================================================================
//  GameTank core top
//
//  Everything console-side lives under this module; the MiSTer `emu` wrapper
//  (GameTank.sv) and the Verilator sim-top (sim/) both instantiate it, so it
//  must never reference sys/ or Quartus-only IP.
//
//  clk_sys is 28.636364 MHz (8× NTSC colorburst); all console logic runs on
//  clock-enables derived from it (CPU/blitter/ACP at ÷8 = 3.579545 MHz,
//  pixel at ÷4 — see docs/HARDWARE.md §Clocks).
//
//  M1: test pattern video, silent audio. The console guts arrive in M2+.
//============================================================================

module gametank
(
    input  logic        clk_sys,
    input  logic        reset,

    // cartridge bus ($8000-$FFFF window; sim-backed until the M7 DDR3 path)
    output logic [14:0] cart_addr,
    input  logic [7:0]  cart_data,

    output logic        ce_pix,
    output logic        hblank,
    output logic        hsync,
    output logic        vblank,
    output logic        vsync,
    output logic [7:0]  video_r,
    output logic [7:0]  video_g,
    output logic [7:0]  video_b,

    output logic [15:0] audio_l,
    output logic [15:0] audio_r
);

assign audio_l = '0;
assign audio_r = '0;

// 3.579545 MHz clock-enable for the console (clk_sys / 8): the CPU's RDY
// strobe. All bus transactions latch at this strobe (see mainbus.sv).
logic [2:0] ce_div;
logic       cpu_ce;

always_ff @(posedge clk_sys) begin
    if (reset) begin
        ce_div <= '0;
        cpu_ce <= 1'b0;
    end
    else begin
        ce_div <= ce_div + 3'd1;
        cpu_ce <= (ce_div == 3'd7);
    end
end

logic        vram_a_page;
logic [13:0] vram_a_addr;
logic        vram_a_we;
logic [7:0]  vram_a_din, vram_a_dout;
logic [13:0] vram_b_addr;
logic [7:0]  vram_b_dout;
logic [7:0]  dma_ctl;

mainbus mainbus
(
    .clk_sys   (clk_sys),
    .reset     (reset),
    .cpu_ce    (cpu_ce),

    .cart_addr (cart_addr),
    .cart_data (cart_data),

    .vram_page (vram_a_page),
    .vram_addr (vram_a_addr),
    .vram_we   (vram_a_we),
    .vram_din  (vram_a_din),
    .vram_dout (vram_a_dout),

    .dma_ctl   (dma_ctl),

    .irq       (1'b0),   // blitter IRQ arrives in M4
    .nmi       (1'b0)    // vsync NMI: wired when the real scanout lands
);

vram vram
(
    .clk    (clk_sys),

    .a_page (vram_a_page),
    .a_addr (vram_a_addr),
    .a_we   (vram_a_we),
    .a_din  (vram_a_din),
    .a_dout (vram_a_dout),

    .b_page (dma_ctl[1]),   // VID_OUT_PAGE
    .b_addr (vram_b_addr),
    .b_dout (vram_b_dout)
);

// Scanout drives port B once the real raster generator lands (M3);
// silence the temporarily-unused signals.
assign vram_b_addr = '0;
wire _unused_vram = &{1'b0, vram_b_dout, dma_ctl[7:2], dma_ctl[0]};

testpattern testpattern
(
    .clk    (clk_sys),
    .reset  (reset),

    .ce_pix (ce_pix),
    .hblank (hblank),
    .hsync  (hsync),
    .vblank (vblank),
    .vsync  (vsync),
    .r      (video_r),
    .g      (video_g),
    .b      (video_b)
);

endmodule

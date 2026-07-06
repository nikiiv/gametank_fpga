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

logic [13:0] win_addr;
logic [7:0]  win_din;
logic        cpu_vram_we, cpu_gram_we, blit_param_we;
logic [7:0]  vram_a_dout, gram_b_dout;
logic [13:0] vram_b_addr;
logic [7:0]  vram_b_dout;
logic [7:0]  dma_ctl, bank_reg;
logic        blit_irq;
logic [1:0]  gram_mid;

mainbus mainbus
(
    .clk_sys       (clk_sys),
    .reset         (reset),
    .cpu_ce        (cpu_ce),

    .cart_addr     (cart_addr),
    .cart_data     (cart_data),

    .win_addr      (win_addr),
    .win_din       (win_din),
    .vram_we       (cpu_vram_we),
    .gram_we       (cpu_gram_we),
    .blit_param_we (blit_param_we),
    .vram_dout     (vram_a_dout),
    .gram_dout     (gram_b_dout),

    .dma_ctl       (dma_ctl),
    .bank_reg      (bank_reg),

    .irq           (blit_irq),
    .nmi           (vsync_nmi && dma_ctl[2])   // VSYNC_NMI enable
);

// Blitter and GRAM
logic [18:0] blit_gram_addr;
logic [7:0]  blit_gram_q;
logic        blit_vram_we;
logic [13:0] blit_vram_addr;
logic [7:0]  blit_vram_din;

blitter blitter
(
    .clk_sys    (clk_sys),
    .reset      (reset),
    .cpu_ce     (cpu_ce),

    .param_we   (blit_param_we),
    .param_addr (win_addr[2:0]),
    .param_data (win_din),

    .dma_ctl    (dma_ctl),
    .banking    (bank_reg),

    .gram_addr  (blit_gram_addr),
    .gram_q     (blit_gram_q),

    .vram_we    (blit_vram_we),
    .vram_addr  (blit_vram_addr),
    .vram_din   (blit_vram_din),

    .irq        (blit_irq),
    .gram_mid   (gram_mid)
);

gram gram
(
    .clk    (clk_sys),

    .a_addr (blit_gram_addr),
    .a_dout (blit_gram_q),

    // CPU window: quadrant within the 64 KB bank comes from the blitter's
    // latched counter high bits (HARDWARE.md §Blitter, gram_mid_bits)
    .b_addr ({bank_reg[2:0], gram_mid, win_addr}),
    .b_we   (cpu_gram_we),
    .b_din  (win_din),
    .b_dout (gram_b_dout)
);

// VRAM port A: blitter writes win over the CPU window — they are never
// simultaneously active (blitter writes require COPY_ENABLE=1, CPU window
// access requires COPY_ENABLE=0), and the blitter's 1-clk pulse disturbs
// CPU reads for one clock mid-window only.
vram vram
(
    .clk    (clk_sys),

    .a_page (bank_reg[3]),
    .a_addr (blit_vram_we ? blit_vram_addr : win_addr),
    .a_we   (blit_vram_we | cpu_vram_we),
    .a_din  (blit_vram_we ? blit_vram_din : win_din),
    .a_dout (vram_a_dout),

    .b_page (dma_ctl[1]),   // VID_OUT_PAGE
    .b_addr (vram_b_addr),
    .b_dout (vram_b_dout)
);

logic vsync_nmi;

gtvideo gtvideo
(
    .clk       (clk_sys),
    .reset     (reset),

    .fb_addr   (vram_b_addr),
    .fb_data   (vram_b_dout),

    .ce_pix    (ce_pix),
    .hblank    (hblank),
    .hsync     (hsync),
    .vblank    (vblank),
    .vsync     (vsync),
    .r         (video_r),
    .g         (video_g),
    .b         (video_b),

    .vsync_nmi (vsync_nmi)
);

endmodule

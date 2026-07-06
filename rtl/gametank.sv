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

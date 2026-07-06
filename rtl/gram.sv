//============================================================================
//  GameTank GRAM — 512 KB graphics/asset RAM (AS6C4008 on the real board).
//
//  Port A: blitter source reads (read-only, one per CPU cycle while a copy
//          runs). Port B: CPU access through the $4000 window when
//          CPU_TO_VRAM=0 (address = {banking[2:0], gram_mid_bits, win[13:0]}).
//
//  512 KB ≈ 4 Mbit — the big BRAM tenant (budget in HARDWARE.md §FPGA).
//============================================================================

module gram
(
    input  logic        clk,

    // Port A: blitter
    input  logic [18:0] a_addr,
    output logic [7:0]  a_dout,

    // Port B: CPU window
    input  logic [18:0] b_addr,
    input  logic        b_we,
    input  logic [7:0]  b_din,
    output logic [7:0]  b_dout
);

logic [7:0] mem [0:524287] /*verilator public_flat_rd*/;

always_ff @(posedge clk) begin
    a_dout <= mem[a_addr];
end

always_ff @(posedge clk) begin
    if (b_we)
        mem[b_addr] <= b_din;
    b_dout <= mem[b_addr];
end

endmodule

//============================================================================
//  GameTank video scanout — cycle-honest raster per the schematic-derived
//  timing in docs/HARDWARE.md §Video (Composite_Video_Generator analysis):
//
//    clk = 28.63636 MHz (8× colorburst) — the same master clock as the real
//    console, so one CPU cycle = 8 clk. All counts below in CPU cycles:
//      line   = 227 cycles (1816 clk), frame = 262 progressive lines
//               (59,474 CPU cycles, 60.19 Hz)
//      HBLANK = counts 0-39, HSYNC = counts 16-31
//      pixels = 128 fb pixels of 1.25 cycles (10 clk) each, starting count 63
//      VBLANK = lines 0-15, VSYNC = lines 4-7
//      rows   : row r on lines 16+2r and 17+2r; rows 123-127 never scanned
//      NMI    : one pulse at the start of line 0 (consumer gates with DMA[2])
//
//  Output for the MiSTer framework: ce_pix = clk/2 (14.318 MHz dot), so a
//  line is 908 dots, DE covers 748×246 (dots 160-907, lines 16-261). Each
//  fb pixel is 5 dots. The borders inside DE replay the last latched pixel
//  byte through the palette — authentic behavior of the 74564 pixel latch
//  (left/right borders show pixel 127 of the previously scanned row).
//============================================================================

module gtvideo
(
    input  logic        clk,
    input  logic        reset,

    // vram port B
    output logic [13:0] fb_addr,     // {row, x}
    input  logic [7:0]  fb_data,

    output logic        ce_pix,
    output logic        hblank,
    output logic        hsync,
    output logic        vblank,
    output logic        vsync,
    output logic [7:0]  r,
    output logic [7:0]  g,
    output logic [7:0]  b,

    output logic        vsync_nmi    // 1-CPU-cycle pulse at frame start
);

localparam int H_CLKS   = 1816;      // 227 CPU cycles
localparam int V_LINES  = 262;

// In clk units within the line:
localparam int HBLANK_END = 40 * 8;  // count 40
localparam int HSYNC_ON   = 16 * 8;
localparam int HSYNC_OFF  = 32 * 8;

// Pixel fetch: px0's address is issued at PX_BASE, its data latched
// PX_BASE+2, palette RGB valid PX_BASE+4 — the display slot of pixel k is
// [PX_BASE+4 + 10k, ...+10). Count 63 = clk 504; the +4 pipeline shift is
// well inside the ±2-CLK28 phase uncertainty of the real board.
localparam int PX_BASE   = 500;

logic [10:0] hclk;   // 0..1815
logic [8:0]  line;   // 0..261
logic [6:0]  px;

wire line_end  = (hclk == 11'(H_CLKS - 1));
wire frame_end = line_end && (line == 9'(V_LINES - 1));

always_ff @(posedge clk) begin
    if (reset) begin
        hclk <= '0;
        line <= '0;
    end
    else begin
        hclk <= line_end ? '0 : hclk + 11'd1;
        if (line_end)
            line <= frame_end ? '0 : line + 9'd1;
    end
end

assign vsync_nmi = (line == 0) && (hclk < 8);

// Sync/blank outputs (registered; ce_pix marks dot boundaries = even hclk)
always_ff @(posedge clk) begin
    if (reset)
        ce_pix <= 1'b0;
    else
        ce_pix <= ~hclk[0];  // high on even hclk -> one dot per 2 clk

    hblank <= (hclk < 11'(HBLANK_END));
    hsync  <= (hclk >= 11'(HSYNC_ON)) && (hclk < 11'(HSYNC_OFF));
    vblank <= (line < 9'd16);
    vsync  <= (line >= 9'd4) && (line < 9'd8);
end

// Framebuffer fetch pipeline: address at phase 0, BRAM data latched at
// phase 2, palette RGB valid at phase 4 — pixel k displays during
// [PX_BASE+4+10k, +10). The pixel-byte latch persists through borders and
// blanking (74564 behavior); it is deliberately never cleared.
wire [6:0] row = 7'((line - 9'd16) >> 1);

logic [3:0] phase;
logic       run;
logic [7:0] byte_latch;

always_ff @(posedge clk) begin
    if (reset) begin
        run   <= 1'b0;
        phase <= '0;
        px    <= '0;
    end
    else if (hclk == 11'(PX_BASE - 1)) begin
        phase <= '0;
        px    <= '0;
        run   <= (line >= 9'd16);
    end
    else if (run) begin
        phase <= (phase == 4'd9) ? '0 : phase + 4'd1;
        if (phase == 4'd0)
            fb_addr <= {row, px};
        if (phase == 4'd2)
            byte_latch <= fb_data;
        if (phase == 4'd9) begin
            if (px == 7'd127) run <= 1'b0;
            else             px <= px + 7'd1;
        end
    end
end

logic [23:0] rgb;

palette palette
(
    .clk   (clk),
    .index (byte_latch),
    .rgb   (rgb)
);

wire de = !(hblank || vblank);

assign r = de ? rgb[23:16] : 8'h00;
assign g = de ? rgb[15:8]  : 8'h00;
assign b = de ? rgb[7:0]   : 8'h00;

endmodule

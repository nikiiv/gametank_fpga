//============================================================================
//  GameTank MiSTer core — M1 test pattern generator
//
//  NTSC-shaped raster off the 28.636364 MHz (8× colorburst) system clock:
//  ce_pix = clk/4 = 7.159 MHz (2× colorburst), 455 pixels/line, 262 lines
//  → 60.05 Hz. Active window 360×240.
//
//  Layout (top to bottom): 75% color bars, grayscale ramp, scrolling
//  checkerboard (proves the design is alive), 1-px white border framing
//  the active area (proves blanking alignment).
//
//  This module is replaced by real GameTank video scanout in M3.
//============================================================================

module testpattern
(
    input  logic       clk,
    input  logic       reset,

    output logic       ce_pix,
    output logic       hblank,
    output logic       hsync,
    output logic       vblank,
    output logic       vsync,
    output logic [7:0] r,
    output logic [7:0] g,
    output logic [7:0] b
);

localparam logic [8:0] H_TOTAL    = 9'd455;
localparam logic [8:0] H_ACTIVE   = 9'd360;
localparam logic [8:0] H_SYNC_ON  = 9'd376;
localparam logic [8:0] H_SYNC_OFF = 9'd410;

localparam logic [8:0] V_TOTAL    = 9'd262;
localparam logic [8:0] V_ACTIVE   = 9'd240;
localparam logic [8:0] V_SYNC_ON  = 9'd245;
localparam logic [8:0] V_SYNC_OFF = 9'd248;

logic [1:0] ce_div;
logic [8:0] hc;
logic [8:0] vc;
logic [7:0] frame;

always_ff @(posedge clk) begin
    if (reset) begin
        ce_div <= '0;
        ce_pix <= 1'b0;
        hc     <= '0;
        vc     <= '0;
        frame  <= '0;
    end
    else begin
        ce_div <= ce_div + 2'd1;
        ce_pix <= (ce_div == 2'd3);

        if (ce_pix) begin
            if (hc == H_TOTAL - 9'd1) begin
                hc <= '0;
                if (vc == V_TOTAL - 9'd1) begin
                    vc    <= '0;
                    frame <= frame + 8'd1;
                end
                else vc <= vc + 9'd1;
            end
            else hc <= hc + 9'd1;
        end
    end
end

always_ff @(posedge clk) begin
    if (ce_pix) begin
        hblank <= (hc >= H_ACTIVE);
        hsync  <= (hc >= H_SYNC_ON && hc < H_SYNC_OFF);
        vblank <= (vc >= V_ACTIVE);
        vsync  <= (vc >= V_SYNC_ON && vc < V_SYNC_OFF);
    end
end

// 8 color bars of 45 px: white, yellow, cyan, green, magenta, red, blue, gray
logic [2:0] bar;
always_comb begin
    bar = 3'(hc / 45);
end

logic [7:0] pat_r, pat_g, pat_b;
always_comb begin
    pat_r = '0;
    pat_g = '0;
    pat_b = '0;

    if (hc == 9'd0 || hc == H_ACTIVE - 9'd1 || vc == 9'd0 || vc == V_ACTIVE - 9'd1) begin
        {pat_r, pat_g, pat_b} = {8'hFF, 8'hFF, 8'hFF};             // border
    end
    else if (vc < 9'd160) begin                                    // color bars
        unique case (bar)
            3'd0: {pat_r, pat_g, pat_b} = {8'hBF, 8'hBF, 8'hBF};
            3'd1: {pat_r, pat_g, pat_b} = {8'hBF, 8'hBF, 8'h00};
            3'd2: {pat_r, pat_g, pat_b} = {8'h00, 8'hBF, 8'hBF};
            3'd3: {pat_r, pat_g, pat_b} = {8'h00, 8'hBF, 8'h00};
            3'd4: {pat_r, pat_g, pat_b} = {8'hBF, 8'h00, 8'hBF};
            3'd5: {pat_r, pat_g, pat_b} = {8'hBF, 8'h00, 8'h00};
            3'd6: {pat_r, pat_g, pat_b} = {8'h00, 8'h00, 8'hBF};
            3'd7: {pat_r, pat_g, pat_b} = {8'h23, 8'h23, 8'h23};
        endcase
    end
    else if (vc < 9'd200) begin                                    // gray ramp
        pat_r = hc[7:0];
        pat_g = hc[7:0];
        pat_b = hc[7:0];
    end
    else begin                                                     // scrolling checker
        if (hc[4] ^ vc[4] ^ frame[4])
            {pat_r, pat_g, pat_b} = {8'h50, 8'hA0, 8'h50};
    end
end

always_ff @(posedge clk) begin
    if (ce_pix) begin
        if (hc < H_ACTIVE && vc < V_ACTIVE) begin
            r <= pat_r;
            g <= pat_g;
            b <= pat_b;
        end
        else begin
            r <= '0;
            g <= '0;
            b <= '0;
        end
    end
end

endmodule

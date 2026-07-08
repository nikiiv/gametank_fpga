//============================================================================
//  GameTank blitter — faithful transcription of the emulator's cycle-exact
//  engine (GameTankEmulator blitter.cpp CatchUp; spec in HARDWARE.md
//  §Blitter). One engine step per CPU cycle (cpu_ce), 1 pixel per step.
//
//  Step order within a cycle (emulator "phases"):
//    0: W decrements (or loads on INIT)
//    1: VX/GX increment (GX through the GCARRY rule); on row completion
//       VY/GY/H advance; XRELOAD/INIT reload counters; copy-done clears
//       RUNNING and would set the engine-side pending flag
//    2: RUNNING |= INIT; W reloads on row completion
//    3: pixel op with the post-update counters (GRAM read via the mirrored
//       counters, VRAM write gated by COPY_ENABLE/TRANSPARENCY/WRAPX/WRAPY)
//
//  The CPU-visible IRQ is a separate exact-duration counter: pending fires
//  (W&$7F)×(H&$7F) CPU cycles after the TRIGGER write (what the emulator
//  schedules and games race against); the line is pending && COPY_IRQ with
//  the gate evaluated live. Any TRIGGER write clears pending.
//
//  The pixel datapath spreads over the 8-clock window after the strobe:
//  GRAM address out at +1, data back at +3, VRAM write at +4.
//============================================================================

module blitter
(
    input  logic        clk_sys,
    input  logic        reset,
    input  logic        cpu_ce,

    // parameter writes (routed by mainbus when COPY_ENABLE=1; strobe-latched)
    input  logic        param_we,
    input  logic [2:0]  param_addr,
    input  logic [7:0]  param_data,

    input  logic [7:0]  dma_ctl,     // $2007 (COPY_ENABLE/COLORFILL/GCARRY/IRQ/TRANSP)
    input  logic [7:0]  banking,     // $2005 (GRAM bank, VRAM page, WRAPX/WRAPY)

    // GRAM source port. gram_paddr/gram_want are combinational (the byte the
    // *next* engine step would consume); gram_ready gates the step — with a
    // BRAM GRAM tie gram_ready high, with the DDR3 backend it stalls the
    // engine on row-buffer misses (pixel-write slip; IRQ timing unaffected —
    // see gram_ddr.sv). gram_addr registers at the step; data on gram_q one
    // clock later, as before.
    output logic [18:0] gram_paddr,
    output logic        gram_want,
    input  logic        gram_ready,

    // The engine is starved: the byte the next step needs isn't fetched
    // yet. The core top freezes the console clock-enable on this, so the
    // exact-duration IRQ counter and the pixel engine can never diverge —
    // otherwise an IRQ handler triggering the next blit (the SDK pattern)
    // restarts the engine while it still drains the previous blit's tail
    // (the M8 Ganymede sprite bug; repro: blit_contention).
    output logic        starved,
    output logic [18:0] gram_addr,
    input  logic [7:0]  gram_q,

    // VRAM destination port (page appended by the core top)
    output logic        vram_we,
    output logic [13:0] vram_addr,
    output logic [7:0]  vram_din,

    output logic        irq,
    output logic [1:0]  gram_mid     // {GY'[7], GX'[7]} latch for the CPU window
);

// Parameter registers (survive blits; only counters change during a copy)
logic [7:0] pVX /*verilator public_flat_rd*/, pVY /*verilator public_flat_rd*/,
            pGX /*verilator public_flat_rd*/, pGY /*verilator public_flat_rd*/,
            pW  /*verilator public_flat_rd*/, pH  /*verilator public_flat_rd*/,
            pCOLOR /*verilator public_flat_rd*/;

// Engine state
logic [7:0] cVX, cVY, cGX, cGY, cW, cH;
logic       trigger /*verilator public_flat_rd*/, init, running;

// Exact-duration IRQ
logic        pending;
logic [13:0] dur;
logic        dur_v;

assign irq = pending && dma_ctl[6];

// dma_ctl[5,2:1] and banking[7:6,3] belong to the bus/video side.
wire _unused = &{1'b0, dma_ctl[5], dma_ctl[2:1], banking[7:6], banking[3]};

function automatic logic [7:0] gcarry(input logic [7:0] v);
    return dma_ctl[4] ? v + 8'd1 : {v[7:4], v[3:0] + 4'd1};
endfunction

// ---- engine step (combinational, applied at cpu_ce) ----------------------
logic [7:0] w0, w2, vx1, vy1, gx1, gy1, h1;
logic       rowcomplete, copydone, running1, running2;

always_comb begin
    // phase 0
    w0 = running ? cW - 8'd1 : cW;
    if (init) w0 = {1'b0, pW[6:0]};
    rowcomplete = (w0 == 8'd0);

    // phase 1
    vx1 = running ? cVX + 8'd1 : cVX;
    gx1 = running ? gcarry(cGX) : cGX;
    vy1 = cVY;
    gy1 = cGY;
    h1  = cH;
    if (running && rowcomplete) begin
        vy1 = cVY + 8'd1;
        gy1 = gcarry(cGY);
        h1  = cH - 8'd1;
    end
    if (rowcomplete || init) begin
        vx1 = pVX;
        gx1 = pGX;
    end
    if (init) begin
        vy1 = pVY;
        gy1 = pGY;
        h1  = {1'b0, pH[6:0]};
    end
    copydone = (h1 == 8'd0);
    running1 = copydone ? 1'b0 : running;

    // phase 2
    running2 = running1 | init;
    w2 = rowcomplete ? {1'b0, pW[6:0]} : w0;
end

// Post-update mirrored source counters (full 8-bit complement; the inverted
// bit 7s participate in GRAM quadrant selection)
wire [7:0] gx_eff = pW[7] ? ~gx1 : gx1;
wire [7:0] gy_eff = pH[7] ? ~gy1 : gy1;

// Candidate source byte for the step this cpu_ce would perform
assign gram_paddr = {banking[2:0], gy_eff[7], gx_eff[7],
                     gy_eff[6:0], gx_eff[6:0]};
assign gram_want  = running2 && !dma_ctl[3];   // colorfill reads nothing

wire engine_step = cpu_ce && (!gram_want || gram_ready);
assign starved = gram_want && !gram_ready;

// ---- registers ------------------------------------------------------------
// Pixel-op pipeline: values captured at the strobe, used over the window
logic       px_valid;    // a pixel op happens this CPU cycle
logic [7:0] px_vx, px_vy;
logic [2:0] win_ph;      // clk offset within the CPU window
logic [7:0] colorbus;    // combinational temp for the write stage

always_ff @(posedge clk_sys) begin
    if (reset) begin
        trigger  <= 1'b0;
        init     <= 1'b0;
        running  <= 1'b0;
        pending  <= 1'b0;
        dur_v    <= 1'b0;
        gram_mid <= '0;
        px_valid <= 1'b0;
        win_ph   <= '0;
        vram_we  <= 1'b0;
    end
    else begin
        // parameter port (mainbus strobe-latches; param_we is a 1-clk pulse
        // aligned to the strobe)
        if (param_we) begin
            unique case (param_addr)
                3'd0: pVX <= param_data;
                3'd1: pVY <= param_data;
                3'd2: pGX <= param_data;
                3'd3: pGY <= param_data;
                3'd4: pW  <= param_data;
                3'd5: pH  <= param_data;
                3'd7: pCOLOR <= param_data;
                default: begin  // 3'd6 TRIGGER
                    pending <= 1'b0;
                    trigger <= param_data[0];
                    if (param_data[0]) begin
                        dur   <= 14'(pW[6:0] * pH[6:0]);
                        dur_v <= 1'b1;
                    end
                end
            endcase
        end

        if (cpu_ce) begin
            // exact-duration IRQ: counts real CPU cycles, never stalls
            if (dur_v) begin
                if (dur <= 14'd1) begin
                    pending <= 1'b1;
                    dur_v   <= 1'b0;
                end
                else dur <= dur - 14'd1;
            end
        end

        if (engine_step) begin
            // engine step
            cW  <= w2;
            cVX <= vx1;
            cVY <= vy1;
            cGX <= gx1;
            cGY <= gy1;
            cH  <= h1;
            running <= running2;
            init    <= trigger;
            trigger <= 1'b0;

            // pixel op launch
            px_valid <= running2;
            if (running2) begin
                gram_addr <= gram_paddr;
                gram_mid  <= {gy_eff[7], gx_eff[7]};
                px_vx     <= vx1;
                px_vy     <= vy1;
            end
        end

        if (cpu_ce)
            win_ph <= '0;
        else
            win_ph <= win_ph + 3'd1;

        // VRAM write at window offset +4 (GRAM data valid from +3)
        vram_we <= 1'b0;
        if (px_valid && win_ph == 3'd3) begin
            if (dma_ctl[0]                                    // COPY_ENABLE
                && (dma_ctl[7] || colorbus != 8'd0)           // TRANSPARENCY
                && !(px_vx[7] && banking[4])                  // WRAPX clip
                && !(px_vy[7] && banking[5])) begin           // WRAPY clip
                vram_we   <= 1'b1;
                vram_addr <= {px_vy[6:0], px_vx[6:0]};
                vram_din  <= colorbus;
            end
            px_valid <= 1'b0;
        end
    end
end

assign colorbus = dma_ctl[3] ? ~pCOLOR : gram_q;

endmodule

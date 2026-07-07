//============================================================================
//  GameTank GRAM, DDR3-backed. 512 KB does not fit the Cyclone V's BRAM
//  next to the framework (M4 fitting: 9.7 Mbit demanded vs 5.66 available),
//  so GRAM lives in HPS DDR3 per the plan's fallback, with a prefetching
//  row buffer in front of the blitter.
//
//  Guarantees preserved (docs/HARDWARE.md, docs/REQUIREMENTS.md):
//   - Blitter IRQ timing is untouched (exact W×H duration counter in
//     blitter.sv, independent of the pixel datapath).
//   - Pixel writes may slip a few cycles under DDR3 latency; software
//     cannot observe this — the $4000 window reads open bus while
//     COPY_ENABLE=1, so mid-blit VRAM is unreadable. The engine stalls
//     (blit_ready=0) on buffer misses and self-heals via a direct fetch.
//   - CPU window reads of GRAM stretch the CPU clock-enable (cpu_stall)
//     until DDR3 data lands; writes are posted.
//
//  Prefetch: a blit's whole GRAM access set is deterministic at TRIGGER
//  (params + mode bits). The prefetcher replicates the engine's per-row GY
//  sequence (GCARRY rule + Y-mirror complement) and streams each row's two
//  gx-quadrant halves (2×128 B) into a double row-buffer, staying one row
//  ahead of consumption.
//
//  GRAM address layout (HARDWARE.md §Blitter):
//    addr = {bank[2:0], gy'[7], gx'[7], gy'[6:0], gx'[6:0]}   (19 bits)
//  Row identity excludes the gx bits: {bank, gy'[7], gy'[6:0]} (11 bits).
//  DDR3 placement: byte address GRAM_BASE + addr.
//============================================================================

module gram_ddr
(
    input  logic        clk_sys,
    input  logic        reset,
    input  logic        cpu_ce,

    // parameter-write snoop (same bus the blitter sees)
    input  logic        param_we,
    input  logic [2:0]  param_addr,
    input  logic [7:0]  param_data,
    input  logic [7:0]  dma_ctl,
    input  logic [7:0]  banking,

    // blitter port: comb candidate address/want, ready gates the engine
    // step; the served byte appears on blit_q one clock after the step
    // (BRAM-latency compatible with the old gram.sv path).
    input  logic [18:0] blit_paddr,
    input  logic        blit_want,
    output logic        blit_ready,
    input  logic [18:0] blit_addr,    // registered by the blitter at the step
    output logic [7:0]  blit_q,

    // CPU window channel (strobe-latched pulses from mainbus)
    input  logic [18:0] cpu_addr,
    input  logic        cpu_we,
    input  logic [7:0]  cpu_wdata,
    input  logic        cpu_rd,       // pulses when a window read latched
    output logic [7:0]  cpu_q,
    output logic        cpu_stall,    // freeze the console clock-enables

    // DDRAM client (64-bit Avalon-like, see sys/emu_ports.vh)
    output logic        ddr_rd,
    output logic        ddr_we,
    output logic [28:0] ddr_addr,     // 64-bit word address
    output logic [63:0] ddr_din,
    output logic [7:0]  ddr_be,
    output logic [7:0]  ddr_burstcnt,
    input  logic [63:0] ddr_dout,
    input  logic        ddr_dout_ready,
    input  logic        ddr_busy
);

// DDR3 byte base for GRAM: 0x3000_0000
localparam logic [28:0] GRAM_BASE_W = 29'h0600_0000;  // word (byte>>3)

// Low paddr bits select within a served beat; blit_addr's row bits are
// implied by the slot latched at the step; other bits are engine-side.
wire _unused = &{1'b0, dma_ctl[7:5], dma_ctl[2:0], banking[7:3],
                 blit_paddr[2:0], blit_addr[18:15], blit_addr[13:7]};

function automatic logic [7:0] gcarry(input logic [7:0] v);
    return dma_ctl[4] ? v + 8'd1 : {v[7:4], v[3:0] + 4'd1};
endfunction

// ---------------------------------------------------------------------------
// Param snoop (the prefetcher's own copies)
// ---------------------------------------------------------------------------
logic [7:0] pGY, pH;
logic       trig_pulse;

always_ff @(posedge clk_sys) begin
    trig_pulse <= 1'b0;
    if (param_we) begin
        case (param_addr)
            3'd3: pGY <= param_data;
            3'd5: pH  <= param_data;
            3'd6: if (param_data[0]) trig_pulse <= 1'b1;
            default: ;
        endcase
    end
end

// ---------------------------------------------------------------------------
// Double row-buffer: 2 slots × 256 B (both gx-quadrant halves of one row)
// ---------------------------------------------------------------------------
logic [7:0]  rowbuf [0:511];
logic [10:0] tag   [0:1];    // {bank, gy'[7], gy'[6:0]}
logic [31:0] beatv [0:1];    // per-8-byte-beat valid bits
logic        lru;            // slot to refill next

wire [10:0] want_row = {blit_paddr[18:15], blit_paddr[13:7]};
wire [4:0]  want_beat = {blit_paddr[14], blit_paddr[6:3]};

wire hit0 = (tag[0] == want_row) && beatv[0][want_beat];
wire hit1 = (tag[1] == want_row) && beatv[1][want_beat];
assign blit_ready = !blit_want || dma_ctl[3] || hit0 || hit1;  // colorfill needs no data

// Serve the registered address one clock after the step (BRAM-compatible).
// The serving slot is LATCHED at the granted step: a refill may retag the
// other slot's entry between the step and the engine's win_ph==3 sample,
// so a live tag compare would redirect an in-flight serve to the wrong
// slot (caught by the blit_scene lockstep run).
logic srv_slot;

always_ff @(posedge clk_sys) begin
    if (cpu_ce && blit_want && blit_ready)
        srv_slot <= hit1;
    blit_q <= rowbuf[{srv_slot, blit_addr[14], blit_addr[6:0]}];
end

`ifdef GRAM_DDR_DEBUG
always_ff @(posedge clk_sys) begin
    if (st == BURST_CMD)
        $display("[%0t] FILL slot=%0d half=%0d row=%03x", $time, fill_slot, fill_half, fill_row);
    if (blit_want)
        $display("[%0t] WANT addr=%05x ready=%0d tag0=%03x tag1=%03x srvslot=%0d q=%02x",
                 $time, blit_paddr, blit_ready, tag[0], tag[1], srv_slot, blit_q);
end
`endif

// ---------------------------------------------------------------------------
// Prefetch sequencing + DDR3 FSM
// ---------------------------------------------------------------------------
typedef enum logic [2:0] { IDLE, BURST_CMD, BURST_DATA, CPUW, CPUR, CPUR_WAIT } st_e;
st_e st;

logic [7:0]  next_gy;        // raw engine gy of the next row to prefetch
logic [7:0]  rows_left;
logic        fill_slot;      // slot being filled
logic        fill_half;      // which 128B half (gx'[7])
logic [3:0]  beat_cnt;
logic [10:0] fill_row;
logic        ydir_l;

// Demand gating: beyond the two initial fills, fetch row k+1 only after the
// engine has demonstrably reached row k (the most recently fetched row) —
// the momentary blit_want has gaps (trigger latency, between-pixel windows)
// and must not be trusted for "engine no longer needs this slot".
logic [1:0]  init_fills;
logic [10:0] last_fetch_tag;
logic [10:0] last_req;
logic        last_req_v;

// CPU channel latches
logic        cpuw_pend, cpur_pend;
logic [18:0] cpu_addr_l;
logic [7:0]  cpu_wdata_l;
logic [2:0]  cpu_lane;

assign cpu_stall = cpur_pend;

wire [7:0] row_eff = ydir_l ? ~next_gy : next_gy;

// Miss redirect: engine waiting on a row neither slot holds nor is filling
wire miss_redirect = blit_want && !dma_ctl[3] && !hit0 && !hit1 &&
                     !((st == BURST_CMD || st == BURST_DATA) && fill_row == want_row);

always_ff @(posedge clk_sys) begin
    if (reset) begin
        st        <= IDLE;
        ddr_rd    <= 1'b0;
        ddr_we    <= 1'b0;
        beatv[0]  <= '0;
        beatv[1]  <= '0;
        tag[0]    <= '1;
        tag[1]    <= '1;
        lru       <= 1'b0;
        rows_left <= '0;
        cpuw_pend <= 1'b0;
        cpur_pend <= 1'b0;
    end
    else begin
        // latch CPU channel requests (mutually exclusive with blitter use
        // of the window, but they may arrive while a prefetch burst runs)
        if (cpu_we) begin
            cpuw_pend   <= 1'b1;
            cpu_addr_l  <= cpu_addr;
            cpu_wdata_l <= cpu_wdata;
        end
        if (cpu_rd) begin
            cpur_pend  <= 1'b1;
            cpu_addr_l <= cpu_addr;
        end

        if (blit_want) begin
            last_req   <= want_row;
            last_req_v <= 1'b1;
        end

        // a new trigger restarts prefetch at the blit's first row
        // (colorfill blits read no GRAM — skip)
        if (trig_pulse) begin
            next_gy    <= pGY;
            rows_left  <= dma_ctl[3] ? 8'd0 :
                          (pH[6:0] == '0) ? 8'd128 : {1'b0, pH[6:0]};
            ydir_l     <= pH[7];
            beatv[0]   <= '0;
            beatv[1]   <= '0;
            lru        <= 1'b0;
            init_fills <= 2'd2;
            last_req_v <= 1'b0;
        end

        unique case (st)
            IDLE: begin
                if (cpuw_pend) begin
                    ddr_we       <= 1'b1;
                    ddr_addr     <= GRAM_BASE_W | {13'd0, cpu_addr_l[18:3]};
                    ddr_din      <= {8{cpu_wdata_l}};
                    ddr_be       <= 8'h01 << cpu_addr_l[2:0];
                    ddr_burstcnt <= 8'd1;
                    st           <= CPUW;
                end
                else if (cpur_pend) begin
                    ddr_rd       <= 1'b1;
                    ddr_addr     <= GRAM_BASE_W | {13'd0, cpu_addr_l[18:3]};
                    ddr_be       <= 8'hFF;
                    ddr_burstcnt <= 8'd1;
                    cpu_lane     <= cpu_addr_l[2:0];
                    st           <= CPUR;
                end
                else if (miss_redirect) begin
                    // self-heal: fetch the row the engine is actually asking for
                    fill_row  <= want_row;
                    fill_slot <= lru;
                    fill_half <= 1'b0;
                    tag[lru]  <= want_row;
                    beatv[lru] <= '0;
                    lru       <= ~lru;
                    last_fetch_tag <= want_row;
                    st        <= BURST_CMD;
                end
                else if (rows_left != 0 &&
                         (init_fills != 0 ||
                          (last_req_v && last_req == last_fetch_tag))) begin
                    // initial two rows fill freely; afterwards row k+1 only
                    // once the engine has reached row k (frees the lru slot)
                    fill_row  <= {banking[2:0], row_eff[7], row_eff[6:0]};
                    fill_slot <= lru;
                    fill_half <= 1'b0;
                    tag[lru]  <= {banking[2:0], row_eff[7], row_eff[6:0]};
                    beatv[lru] <= '0;
                    lru       <= ~lru;
                    last_fetch_tag <= {banking[2:0], row_eff[7], row_eff[6:0]};
                    if (init_fills != 0) init_fills <= init_fills - 2'd1;
                    next_gy   <= gcarry(next_gy);
                    rows_left <= rows_left - 8'd1;
                    st        <= BURST_CMD;
                end
            end

            BURST_CMD: begin
                ddr_rd       <= 1'b1;
                // 128-byte half-row: byte addr {fill_row[10:7], fill_half,
                // fill_row[6:0], 7'b0} >> 3 = a 16-bit word offset
                ddr_addr     <= GRAM_BASE_W |
                                {13'd0, fill_row[10:7], fill_half, fill_row[6:0], 4'd0};
                ddr_be       <= 8'hFF;
                ddr_burstcnt <= 8'd16;
                beat_cnt     <= '0;
                st           <= BURST_DATA;
            end

            BURST_DATA: begin
                if (!ddr_busy) ddr_rd <= 1'b0;
                if (ddr_dout_ready) begin
                    for (int b = 0; b < 8; b++)
                        rowbuf[{fill_slot, fill_half, beat_cnt, 3'(b)}] <= ddr_dout[b*8 +: 8];
                    beatv[fill_slot][{fill_half, beat_cnt}] <= 1'b1;
                    beat_cnt <= beat_cnt + 4'd1;
                    if (beat_cnt == 4'd15) begin
                        if (!fill_half) begin
                            fill_half <= 1'b1;
                            st <= BURST_CMD;
                        end
                        else st <= IDLE;
                    end
                end
            end

            CPUW: begin
                if (!ddr_busy) begin
                    ddr_we    <= 1'b0;
                    cpuw_pend <= 1'b0;
                    st        <= IDLE;
                end
            end

            CPUR: begin
                if (!ddr_busy) begin
                    ddr_rd <= 1'b0;
                    st     <= CPUR_WAIT;
                end
            end

            CPUR_WAIT: begin
                if (ddr_dout_ready) begin
                    cpu_q      <= ddr_dout[cpu_lane*8 +: 8];
                    cpur_pend  <= 1'b0;
                    st         <= IDLE;
                end
            end

            default: st <= IDLE;
        endcase
    end
end

endmodule

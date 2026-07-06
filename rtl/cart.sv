//============================================================================
//  GameTank cartridge controller — DDR3-backed .gtr behind the abstract
//  cart bus (HARDWARE.md §Cartridge; emulator reference gte.cpp:275-330).
//
//  Formats (inferred from download size, as the emulator does):
//    8 KB  EEPROM  — image mirrored across the whole $8000-$FFFF window
//    32 KB EEPROM  — image mapped 1:1
//    2 MB  Flash2M — $C000-$FFFF fixed last 16 KB, $8000-$BFFF a 16 KB
//                    bank selected by bank_mask[6:0]
//
//  The 8-bit bank register is a 74HC595 loaded by VIA Port A bit-banged
//  SPI: PA0 = shift clock, PA1 = data, PA2 = latch (also flash CS). Shift
//  happens on the PA0 rising edge with the *pre-edge* PA1 value, latch on
//  the PA2 rising edge; bit 7 is forced high (only the RAM32K cart variant
//  clears it, and that variant is post-1.0 — as is all flash writing:
//  Phase 1 carts are read-only per REQUIREMENTS.md, so PA7/MISO idles
//  high via the port pull-up).
//
//  Latency plan (6502 bus cycle = 8 clk = 280 ns; DDR round trips are
//  longer and variable):
//    - $C000-$FFFF (vectors + hot code) is copied into a 16 KB BRAM right
//      after download, so the fixed bank never touches DDR3 at run time.
//    - $8000-$BFFF is served from two 8-byte word buffers, parity-mapped
//      (even image words in slot 0, odd in slot 1). A miss freezes the
//      CPU clock-enable (cart_stall, same mechanism as the GRAM CPU
//      window) and fetches the missed word plus the next in one 2-beat
//      burst, so sequential code stalls once per 16 bytes (~30 clk, a
//      documented timing deviation). Buffers mutate only while the CPU
//      is stalled — hit lookups can never race a refill (the M7
//      async-prefetch lesson).
//
//  The image lives in HPS DDR3 at byte 0x3010_0000 (above GRAM's 512 KB
//  at 0x3000_0000), reached through rtl/ddr_mux.sv. The download port is
//  driven by ioctl in the MiSTer wrapper and by the sim harness in
//  simulation — the download machinery deliberately ignores the console
//  reset (the wrapper holds the core in reset for the whole transfer;
//  power-up state comes from constant initializers). Until a download
//  completes (cart_present=0) the core top serves the window from its
//  built-in boot cart instead.
//============================================================================

module cart
(
    input  logic        clk_sys,
    input  logic        reset,          // console reset (held during download)

    // console window (strobe-latched by mainbus)
    input  logic [14:0] cart_addr,
    input  logic        cart_rd,        // 1-clk pulse after the CPU strobe
    output logic [7:0]  cart_data,      // valid by the next strobe
    output logic        cart_stall,     // freeze cpu_ce while a miss is out
    output logic        cart_present,

    // VIA Port A pin levels (bank shift register)
    input  logic [7:0]  via_pa,

    // download stream (ioctl / sim harness)
    input  logic        dl_active,
    input  logic        dl_wr,          // 1-clk pulse per byte
    input  logic [20:0] dl_addr,
    input  logic [7:0]  dl_data,
    output logic        dl_busy,        // backpressure (→ ioctl_wait)

    // DDR3 client (via ddr_mux)
    output logic        ddr_rd,
    output logic        ddr_we,
    output logic [28:0] ddr_addr,
    output logic [63:0] ddr_din,
    output logic [7:0]  ddr_be,
    output logic [7:0]  ddr_burstcnt,
    input  logic [63:0] ddr_dout,
    input  logic        ddr_dout_ready,
    input  logic        ddr_busy
);

// Base word address (byte 0x3010_0000). NB: added, never OR'd, onto
// offsets — the base has word bit 17 set, and 2 MB of image needs 18
// offset bits, so OR would alias the image's upper megabyte onto the
// lower (the M7 fill bug).
localparam logic [28:0] CART_BASE_W = 29'h0602_0000;

typedef enum logic [1:0] { T_EEPROM8K, T_EEPROM32K, T_FLASH2M } cart_t;

// ---------------------------------------------------------------------------
// Bank shift register (74HC595 on VIA Port A)
// ---------------------------------------------------------------------------
logic [7:0] pa_q;
logic [7:0] bank_shift;
logic [7:0] bank_mask /*verilator public_flat_rd*/;

// only PA0 (clock), PA1 (data), PA2 (latch) matter; bank_mask[7] is the
// RAM32K save-RAM select, forced high in M7 (no RAM32K support)
wire _unused = &{1'b0, pa_q[7:3], bank_mask[7], fill_off[2:0]};

always_ff @(posedge clk_sys) begin
    pa_q <= via_pa;
    if (reset) begin
        bank_shift <= 8'hFF;
        bank_mask  <= 8'hFF;    // undefined at power-on; pull-up-ish default
    end
    else begin
        if (via_pa[0] && !pa_q[0])          // shift clock rising edge
            bank_shift <= {bank_shift[6:0], pa_q[1]};   // pre-edge data
        if (via_pa[2] && !pa_q[2])          // latch (CS) rising edge
            bank_mask <= bank_shift | 8'h80;  // [7] forced: no RAM32K in M7
    end
end

// ---------------------------------------------------------------------------
// Fixed-bank BRAM ($C000-$FFFF): 2048×64, one DDR beat per word, byte
// lane-muxed on the console side. Filled once right after download.
// ---------------------------------------------------------------------------
logic [63:0] fixedram [0:2047] /*verilator public_flat_rd*/;
logic [10:0] fx_word;
logic [2:0]  fx_lane;
logic [63:0] fixed_qw;

// ---------------------------------------------------------------------------
// Banked-window word buffers, parity-mapped: slot = image word bit 0.
// Tags are one bit wider than the 18-bit word address so the word past
// the image end (fetched as the second burst beat at the top of the
// last bank) can never alias word 0.
// ---------------------------------------------------------------------------
logic [63:0] buf_d [0:1];
logic [18:0] buf_tag [0:1];
logic [1:0]  buf_v;

// image byte offset of a banked-window ($8000-$BFFF) access
function automatic logic [20:0] img_off(input cart_t t, input logic [6:0] bank,
                                        input logic [13:0] a);
    case (t)
        T_FLASH2M:   img_off = {bank, a};
        T_EEPROM32K: img_off = {7'd0, a};
        default:     img_off = {8'd0, a[12:0]};   // 8 KB mirror
    endcase
endfunction

// fixed-bank fill source (byte offset of the $C000 region in the image)
function automatic logic [20:0] fill_src(input cart_t t, input logic [13:0] i);
    case (t)
        T_FLASH2M:   fill_src = {7'h7F, i};       // last 16 KB
        T_EEPROM32K: fill_src = {7'd1, i};        // upper 16 KB
        default:     fill_src = {8'd0, i[12:0]};  // 8 KB mirrored ×2
    endcase
endfunction

// ---------------------------------------------------------------------------
// Download bookkeeping (survives console reset by design)
// ---------------------------------------------------------------------------
logic        dl_active_q = 1'b0;
logic [21:0] dl_size     = '0;         // bytes (max addr written + 1)
cart_t       ctype       = T_EEPROM32K;
logic        present_r /*verilator public_flat_rd*/ = 1'b0;
assign cart_present = present_r;

wire dl_start = dl_active && !dl_active_q;
wire dl_end   = !dl_active && dl_active_q;

logic        dlw_pend = 1'b0;          // one buffered download byte
logic [20:0] dlw_addr;
logic [7:0]  dlw_data;
assign dl_busy = dlw_pend;

// ---------------------------------------------------------------------------
// Console read service
// ---------------------------------------------------------------------------
wire [20:0] rd_off  = img_off(ctype, bank_mask[6:0], cart_addr[13:0]);
wire [18:0] rd_word = {1'b0, rd_off[20:3]};

wire hit = buf_v[rd_word[0]] && (buf_tag[rd_word[0]] == rd_word);

logic        serve_fixed;              // last read was $C000+
logic [2:0]  serve_lane;
logic [7:0]  bank_q;

logic        miss_pend /*verilator public_flat_rd*/;   // demand fetch outstanding
logic [18:0] miss_word;

assign cart_stall = miss_pend;
assign cart_data  = serve_fixed ? fixed_qw[fx_lane*8 +: 8] : bank_q;

// ---------------------------------------------------------------------------
// DDR machine: download writes, fixed-bank fill (16-word bursts), demand
// fetches and prefetches (1-word bursts). Runs through console reset.
// ---------------------------------------------------------------------------
typedef enum logic [2:0] { IDLE, DLW, FILL_CMD, FILL_DATA, FETCH }
    dstate_t;

wire [20:0] fill_off = fill_src(ctype, fill_idx);   // Quartus 17 can't
wire        fetch_other = ~miss_word[0];            // bit-select a call
dstate_t st /*verilator public_flat_rd*/ = IDLE;

logic        filling = 1'b0;
logic [13:0] fill_idx;                 // dest byte offset (128-aligned)
logic [3:0]  beat_cnt;
logic        fetch_beat1;              // second beat of the 2-word fetch

always_ff @(posedge clk_sys) begin
    // ---- fixed-bank BRAM (write port shared with the fill burst) ---------
    if (st == FILL_DATA && ddr_dout_ready)
        fixedram[{fill_idx[13:7], beat_cnt}] <= ddr_dout;
    fixed_qw <= fixedram[fx_word];

    // ---- console-side request latching (reset-sensitive) -----------------
    if (reset) begin
        buf_v       <= 2'b00;
        miss_pend   <= 1'b0;
        serve_fixed <= 1'b1;
    end
    // no download yet → the window is served by the external boot cart;
    // stay silent (no stalls, no DDR traffic)
    else if (cart_rd && present_r) begin
        serve_fixed <= cart_addr[14];
        if (cart_addr[14]) begin
            fx_word <= cart_addr[13:3];
            fx_lane <= cart_addr[2:0];
        end
        else begin
            serve_lane <= rd_off[2:0];
            if (hit)
                bank_q <= buf_d[rd_word[0]][rd_off[2:0]*8 +: 8];
            else begin
                miss_pend <= 1'b1;
                miss_word <= rd_word;
            end
        end
    end

    // ---- download bookkeeping (ignores console reset) --------------------
    dl_active_q <= dl_active;
    if (dl_start) begin
        dl_size   <= '0;
        present_r <= 1'b0;
    end
    if (dl_wr && !dlw_pend) begin
        dlw_pend <= 1'b1;
        dlw_addr <= dl_addr;
        dlw_data <= dl_data;
        if ({1'b0, dl_addr} >= dl_size)
            dl_size <= {1'b0, dl_addr} + 22'd1;
    end
    if (dl_end) begin
        ctype    <= (dl_size <= 22'h02000) ? T_EEPROM8K  :
                    (dl_size <= 22'h08000) ? T_EEPROM32K : T_FLASH2M;
        filling  <= 1'b1;
        fill_idx <= '0;
    end

    // ---- DDR machine ------------------------------------------------------
    unique case (st)
        IDLE: begin
            if (dlw_pend) begin
                ddr_we       <= 1'b1;
                ddr_addr     <= CART_BASE_W + {11'd0, dlw_addr[20:3]};
                ddr_din      <= {8{dlw_data}};
                ddr_be       <= 8'h01 << dlw_addr[2:0];
                ddr_burstcnt <= 8'd1;
                st           <= DLW;
            end
            else if (filling && !dl_active)
                st <= FILL_CMD;
            else if (miss_pend) begin
                ddr_rd       <= 1'b1;
                ddr_addr     <= CART_BASE_W + {10'd0, miss_word};
                ddr_be       <= 8'hFF;
                ddr_burstcnt <= 8'd2;   // missed word + the next one
                fetch_beat1  <= 1'b0;
                st           <= FETCH;
            end
        end

        DLW: begin
            if (!ddr_busy) begin
                ddr_we   <= 1'b0;
                dlw_pend <= 1'b0;
                st       <= IDLE;
            end
        end

        FILL_CMD: begin
            ddr_rd       <= 1'b1;
            ddr_addr     <= CART_BASE_W + {11'd0, fill_off[20:3]};
            ddr_be       <= 8'hFF;
            ddr_burstcnt <= 8'd16;
            beat_cnt     <= '0;
            st           <= FILL_DATA;
        end

        FILL_DATA: begin
            if (!ddr_busy) ddr_rd <= 1'b0;
            if (ddr_dout_ready) begin
                beat_cnt <= beat_cnt + 4'd1;
                if (beat_cnt == 4'd15) begin
                    if (fill_idx == 14'h3F80) begin      // 16 KB done
                        filling   <= 1'b0;
                        present_r <= 1'b1;
                        st        <= IDLE;
                    end
                    else begin
                        fill_idx <= fill_idx + 14'd128;
                        st       <= FILL_CMD;
                    end
                end
            end
        end

        FETCH: begin
            // the CPU is stalled for the whole 2-beat refill, so these
            // buffer writes cannot race a hit lookup
            if (!ddr_busy) ddr_rd <= 1'b0;
            if (ddr_dout_ready) begin
                if (!fetch_beat1) begin
                    buf_d[miss_word[0]]   <= ddr_dout;
                    buf_tag[miss_word[0]] <= miss_word;
                    buf_v[miss_word[0]]   <= 1'b1;
                    bank_q                <= ddr_dout[serve_lane*8 +: 8];
                    fetch_beat1           <= 1'b1;
                end
                else begin
                    buf_d[fetch_other]   <= ddr_dout;
                    buf_tag[fetch_other] <= miss_word + 19'd1;
                    buf_v[fetch_other]   <= 1'b1;
                    miss_pend              <= 1'b0;
                    st                     <= IDLE;
                end
            end
        end

        default: st <= IDLE;
    endcase
end

endmodule

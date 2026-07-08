//============================================================================
//  GameTank flash-save streamer (M9.1) — persists the DDR3 cart image's
//  flash state to a MiSTer save file and restores it on load.
//
//  The desktop emulator (the compatibility floor) persists flash when the
//  game issues the AMD `$90` command prefix — the SDK save idiom — by
//  writing a full-image diff next to the ROM. This core saves the raw
//  modified 2 MB image as MiSTer-conventional `saves/GameTank/<game>.sav`
//  (decision + reconsideration paths in REQUIREMENTS.md §scope cuts).
//
//  Restore reuses the cartridge download port: the mounted .sav is
//  streamed through the same byte interface as a .gtr download, which
//  re-derives the image size/type, refills the fixed-bank BRAM and sweeps
//  the cart cache for free (the console is held in reset by the cart's
//  dl_wait for the duration, exactly like a load). Save reads the image
//  back out of DDR3 through a dedicated read-only ddr_mux client and
//  serves it to the framework 512-byte block by block; the console keeps
//  running during a save, so a game that flash-writes mid-save can tear
//  it — the emulator's threaded save has the same window.
//
//  Save triggers: the game's `$90` flash command (primary, emulator-
//  matched), the OSD "Save Backup RAM" entry, and OSD-open autosave when
//  a `$90` has been seen since the last save. Restore triggers: save-file
//  mount after a cart download, and the OSD "Load Backup RAM" entry.
//============================================================================

module savectl
(
    input  logic        clk_sys,

    // hps_io SD block interface (save slot 0, 512-byte blocks)
    output logic [31:0] sd_lba,
    output logic        sd_rd,
    output logic        sd_wr,
    input  logic        sd_ack,
    input  logic [8:0]  sd_buff_addr,
    input  logic [7:0]  sd_buff_dout,
    output logic [7:0]  sd_buff_din,
    input  logic        sd_buff_wr,
    input  logic        img_mounted,      // pulse on (re)mount
    input  logic        img_readonly,
    input  logic [63:0] img_size,

    // control
    input  logic        bk_load,          // OSD: Load Backup RAM
    input  logic        bk_save,          // OSD: Save Backup RAM
    input  logic        autosave,         // OSD option
    input  logic        osd_open,         // OSD_STATUS
    input  logic        save_trigger,     // $90 flash command (from cart)
    input  logic        cart_dl_active,   // real ioctl download running
    input  logic        cart_busy,        // cart dl_wait (download/fill/restore)
    input  logic        cart_present,

    // restore stream into the cart download port
    output logic        sv_active,
    output logic        sv_wr,
    output logic [20:0] sv_addr,
    output logic [7:0]  sv_data,
    input  logic        sv_busy,          // cart dl_busy

    // DDR3 read client (via ddr_mux, read-only)
    output logic        c2_rd,
    output logic [28:0] c2_addr,
    output logic [7:0]  c2_burstcnt,
    input  logic [63:0] ddr_dout,
    input  logic        c2_dout_ready,
    input  logic        c2_busy
);

// cart image base in DDR3, word address (see rtl/cart.sv)
localparam logic [28:0] CART_BASE_W = 29'h0602_0000;
localparam logic [31:0] SAVE_BLOCKS = 32'd4096;   // 2 MB / 512

// ---------------------------------------------------------------------------
// Mount bookkeeping and trigger edges
// ---------------------------------------------------------------------------
logic mounted = 1'b0, readonly_r = 1'b0, dirty = 1'b0;
logic restore_pend = 1'b0, save_pend = 1'b0;
logic load_q, save_q, osd_q;

// ---------------------------------------------------------------------------
// 512-byte block buffer (shared by both directions; the FSM serializes)
// ---------------------------------------------------------------------------
logic [7:0] bb [0:511];
logic [8:0] bb_addr;
logic [7:0] bb_q;
logic       bb_we;
logic [7:0] bb_wd;

always_ff @(posedge clk_sys) begin
    if (bb_we) bb[bb_addr] <= bb_wd;
    bb_q <= bb[bb_raddr];
end

// the framework reads save data with sd_buff_addr; serve one clock behind
// (registered BRAM output — the standard core-side arrangement)
assign sd_buff_din = bb_q;

// ---------------------------------------------------------------------------
// FSM
// ---------------------------------------------------------------------------
typedef enum logic [3:0] {
    IDLE,
    // restore: fetch a block from the file, then drain it into the cart
    R_REQ, R_FILL, R_DRAIN,
    // save: fill the buffer from DDR3, then hand the block to the file
    S_READ, S_WAIT, S_UNPACK, S_REQ, S_XFER
} st_t;
st_t st /*verilator public_flat_rd*/ = IDLE;

// read side of bb: during a block upload to the framework, follow its
// address directly so sd_buff_din lags sd_buff_addr by exactly one clock
// (the standard registered-BRAM arrangement); otherwise the FSM's cursor.
wire [8:0] bb_raddr = (st == S_XFER) ? sd_buff_addr : bb_addr;

logic [31:0] blk;                 // current block
logic [8:0]  drain_i;             // byte cursor within a block
logic [5:0]  word_i;              // word cursor within a block (save fill)
logic [2:0]  lane_i;              // byte cursor within a word
logic [63:0] word_r;
logic        ack_q;
logic        drain_hold;          // one-clk gap after each accepted dl byte

assign c2_burstcnt = 8'd1;
assign sv_data     = bb_q;

always_ff @(posedge clk_sys) begin
    ack_q  <= sd_ack;
    load_q <= bk_load;
    save_q <= bk_save;
    osd_q  <= osd_open;

    if (img_mounted) begin
        // any mount enables saving (the framework mounts a zero-size file
        // when no save exists yet); only a non-empty file restores
        mounted    <= 1'b1;
        readonly_r <= img_readonly;
        restore_pend <= |img_size;
        dirty        <= 1'b0;
    end

    // Saving is enabled whenever a cartridge is present — not gated on a
    // mounted save file. The MiSTer framework associates <rom>.sav with the
    // save slot on ROM load and creates it on the first sd_wr, so the first
    // save must be able to run before any file exists (img_mounted only
    // signals an EXISTING file, used to gate restore).
    if (save_trigger && cart_present) begin
        dirty <= 1'b1;
        if (!readonly_r) save_pend <= 1'b1;
    end
    if (bk_save && !save_q && cart_present && !readonly_r)
        save_pend <= 1'b1;
    if (osd_open && !osd_q && autosave && dirty && cart_present && !readonly_r)
        save_pend <= 1'b1;
    if (bk_load && !load_q && mounted)
        restore_pend <= 1'b1;

    bb_we      <= 1'b0;
    sv_wr      <= 1'b0;
    drain_hold <= 1'b0;

    unique case (st)
        IDLE: begin
            sd_rd     <= 1'b0;
            sd_wr     <= 1'b0;
            sv_active <= 1'b0;
            c2_rd     <= 1'b0;
            // never start while a real download or the cart's fill runs;
            // restore outranks save (it only pends right after a mount)
            if (!cart_dl_active && !cart_busy) begin
                if (restore_pend) begin
                    restore_pend <= 1'b0;
                    sv_active    <= 1'b1;
                    blk          <= 32'd0;
                    st           <= R_REQ;
                end
                else if (save_pend && cart_present) begin
                    save_pend <= 1'b0;
                    dirty     <= 1'b0;
                    blk       <= 32'd0;
                    word_i    <= 6'd0;
                    st        <= S_READ;
                end
            end
        end

        // ---- restore ------------------------------------------------------
        R_REQ: begin
            sd_lba <= blk;
            sd_rd  <= 1'b1;
            if (sd_ack && !ack_q) sd_rd <= 1'b0;
            if (sd_ack) st <= R_FILL;
        end

        R_FILL: begin
            // hps streams the block in via sd_buff_wr
            if (sd_buff_wr) begin
                bb_we   <= 1'b1;
                bb_addr <= sd_buff_addr;
                bb_wd   <= sd_buff_dout;
            end
            if (!sd_ack && ack_q) begin
                drain_i <= 9'd0;
                bb_addr <= 9'd0;      // prime bb_q for byte 0
                st      <= R_DRAIN;
            end
        end

        R_DRAIN: begin
            // one byte per dl-port transaction; bb_q lags bb_addr by a clock,
            // so issue the byte one cycle after moving the cursor
            if (!sv_busy && !sv_wr && !drain_hold) begin
                sv_wr      <= 1'b1;
                sv_addr    <= {blk[11:0], drain_i};
                drain_hold <= 1'b1;
                if (drain_i == 9'd511) begin
                    if (blk == SAVE_BLOCKS - 1) begin
                        sv_active <= 1'b0;   // dl_end → cart refills + sweeps
                        st        <= IDLE;
                    end
                    else begin
                        blk <= blk + 32'd1;
                        st  <= R_REQ;
                    end
                end
                else begin
                    drain_i <= drain_i + 9'd1;
                    bb_addr <= drain_i + 9'd1;
                end
            end
        end

        // ---- save ---------------------------------------------------------
        S_READ: begin
            c2_rd   <= 1'b1;
            c2_addr <= CART_BASE_W + {11'd0, blk[11:0], word_i};
            st      <= S_WAIT;
        end

        S_WAIT: begin
            if (!c2_busy) c2_rd <= 1'b0;
            if (c2_dout_ready) begin
                word_r <= ddr_dout;
                lane_i <= 3'd0;
                st     <= S_UNPACK;
            end
        end

        S_UNPACK: begin
            bb_we   <= 1'b1;
            bb_addr <= {word_i, lane_i};
            bb_wd   <= word_r[lane_i*8 +: 8];
            if (lane_i == 3'd7) begin
                if (word_i == 6'd63) begin
                    // NB: no bb_addr reset here — it would collide with the
                    // {word_i,lane_i}=511 write above (last assignment wins,
                    // clobbering bb[0] with byte 511). S_XFER reads via
                    // sd_buff_addr, so the cursor need not be reset.
                    word_i  <= 6'd0;
                    st      <= S_REQ;
                end
                else begin
                    word_i <= word_i + 6'd1;
                    st     <= S_READ;
                end
            end
            else lane_i <= lane_i + 3'd1;
        end

        S_REQ: begin
            sd_lba <= blk;
            sd_wr  <= 1'b1;
            if (sd_ack && !ack_q) sd_wr <= 1'b0;
            if (sd_ack) st <= S_XFER;
        end

        S_XFER: begin
            // hps drives sd_buff_addr; bb_raddr follows it combinationally,
            // so sd_buff_din (bb_q) is one registered clock behind
            if (!sd_ack && ack_q) begin
                if (blk == SAVE_BLOCKS - 1) st <= IDLE;
                else begin
                    blk <= blk + 32'd1;
                    st  <= S_READ;
                end
            end
        end

        default: st <= IDLE;
    endcase
end

endmodule

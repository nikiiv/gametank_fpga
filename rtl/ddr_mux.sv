//============================================================================
//  Two-client arbiter for the single MiSTer DDRAM port.
//
//  Client 0 = GRAM prefetcher (rtl/gram_ddr.sv), client 1 = cartridge
//  (rtl/cart.sv). Both speak the stock ddram protocol: hold rd/we until
//  !busy (command accepted), then reads deliver `burstcnt` beats on
//  dout_ready. The arbiter grants a client, passes its signals through,
//  and releases when the transaction completes (write: command accepted;
//  read: all beats delivered). A non-granted client sees busy=1 and
//  dout_ready=0, so it simply keeps its command asserted — no protocol
//  change on either side. Grants alternate on contention (round-robin);
//  both clients tolerate added latency by design (blitter stalls, CPU
//  clock-enable stretch), so fairness matters more than priority.
//============================================================================

module ddr_mux
(
    input  logic        clk_sys,

    // client 0 (GRAM)
    input  logic        c0_rd,
    input  logic        c0_we,
    input  logic [28:0] c0_addr,
    input  logic [63:0] c0_din,
    input  logic [7:0]  c0_be,
    input  logic [7:0]  c0_burstcnt,
    output logic        c0_dout_ready,
    output logic        c0_busy,

    // client 1 (cartridge)
    input  logic        c1_rd,
    input  logic        c1_we,
    input  logic [28:0] c1_addr,
    input  logic [63:0] c1_din,
    input  logic [7:0]  c1_be,
    input  logic [7:0]  c1_burstcnt,
    output logic        c1_dout_ready,
    output logic        c1_busy,

    // upstream DDRAM port (dout fans out to both clients unqualified;
    // dout_ready is the per-client qualifier)
    output logic        ddr_rd,
    output logic        ddr_we,
    output logic [28:0] ddr_addr,
    output logic [63:0] ddr_din,
    output logic [7:0]  ddr_be,
    output logic [7:0]  ddr_burstcnt,
    input  logic        ddr_dout_ready,
    input  logic        ddr_busy
);

typedef enum logic [1:0] { IDLE, CMD, BEATS } state_t;
// No reset: the arbiter must keep serving cart downloads while the
// wrapper holds the console in reset. Power-up state from initializers.
state_t     st         = IDLE;
logic       owner      = 1'b0;   // which client holds the grant
logic       last_owner = 1'b1;   // round-robin memory
logic [7:0] beats_left = '0;

wire c0_req = c0_rd | c0_we;
wire c1_req = c1_rd | c1_we;

wire granted0 = (st != IDLE) && (owner == 1'b0);
wire granted1 = (st != IDLE) && (owner == 1'b1);

// pass-through for the granted client
assign ddr_rd       = granted0 ? c0_rd  : granted1 ? c1_rd  : 1'b0;
assign ddr_we       = granted0 ? c0_we  : granted1 ? c1_we  : 1'b0;
assign ddr_addr     = granted1 ? c1_addr     : c0_addr;
assign ddr_din      = granted1 ? c1_din      : c0_din;
assign ddr_be       = granted1 ? c1_be       : c0_be;
assign ddr_burstcnt = granted1 ? c1_burstcnt : c0_burstcnt;

assign c0_busy       = granted0 ? ddr_busy : 1'b1;
assign c1_busy       = granted1 ? ddr_busy : 1'b1;
assign c0_dout_ready = granted0 && ddr_dout_ready;
assign c1_dout_ready = granted1 && ddr_dout_ready;

always_ff @(posedge clk_sys) begin
    unique case (st)
            IDLE: begin
                if (c0_req || c1_req) begin
                    // round-robin: on contention the other client goes first
                    owner <= (c0_req && c1_req) ? ~last_owner : c1_req;
                    st    <= CMD;
                end
            end

            CMD: begin
                // wait for the owner's command to be accepted
                if (ddr_rd && !ddr_busy) begin
                    beats_left <= ddr_burstcnt;
                    st         <= BEATS;
                end
                else if (ddr_we && !ddr_busy) begin
                    last_owner <= owner;
                    st         <= IDLE;
                end
                else if (!ddr_rd && !ddr_we) begin
                    // owner withdrew (shouldn't happen mid-protocol; recover)
                    last_owner <= owner;
                    st         <= IDLE;
                end
            end

            BEATS: begin
                if (ddr_dout_ready) begin
                    beats_left <= beats_left - 8'd1;
                    if (beats_left == 8'd1) begin
                        last_owner <= owner;
                        st         <= IDLE;
                    end
                end
            end

        default: st <= IDLE;
    endcase
end

endmodule

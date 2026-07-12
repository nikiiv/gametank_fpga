//============================================================================
//  Emulator-style framebuffer presentation.
//
//  GameTank software owns two 128x128 framebuffers and is allowed to recycle
//  a released page immediately. The desktop emulator samples VID_OUT_PAGE
//  once per host frame and presents an immutable image; directly scanning a
//  hardware page cannot reproduce that ownership model when a game flips and
//  starts drawing the old page during active video.
//
//  At the start of vertical blank, copy the currently requested 16 KiB page
//  through VRAM's read port into a dedicated display snapshot. The copy takes
//  16,386 CLK28 cycles including the synchronous-read pipeline, comfortably
//  inside the 29,056-cycle vertical blank. Active scanout only reads the
//  snapshot, so register transients and writes to either game-owned page can
//  never splice partial frames into the output.
//============================================================================

module frame_snapshot
(
    input  logic        clk,
    input  logic        reset,
    input  logic        vblank,
    input  logic        requested_page,

    output logic        source_page,
    output logic [13:0] source_addr,
    input  logic [7:0]  source_data,

    input  logic [13:0] scan_addr,
    output logic [7:0]  scan_data
);

logic [7:0] mem [0:16383];

logic        vblank_q;
logic        copying;
logic        write_valid;
logic [13:0] write_addr;

always_ff @(posedge clk) begin
    // Same one-clock synchronous-read shape as VRAM's original video port.
    scan_data <= mem[scan_addr];

    if (write_valid)
        mem[write_addr] <= source_data;

    if (reset) begin
        vblank_q    <= 1'b0;
        copying     <= 1'b0;
        write_valid <= 1'b0;
        source_page <= 1'b0;
        source_addr <= '0;
        write_addr  <= '0;
    end
    else begin
        vblank_q <= vblank;

        // source_data is the byte requested one clock earlier from VRAM.
        write_valid <= copying;
        if (copying) begin
            write_addr <= source_addr;
            if (source_addr == 14'h3FFF)
                copying <= 1'b0;
            else
                source_addr <= source_addr + 14'd1;
        end

        // Start after the vblank edge has propagated to this module. The
        // first source byte is issued on the following clock, so no stale
        // pre-edge VRAM output is committed to the snapshot.
        if (vblank && !vblank_q) begin
            source_page <= requested_page;
            source_addr <= '0;
            copying <= 1'b1;
            write_valid <= 1'b0;
        end
    end
end

endmodule

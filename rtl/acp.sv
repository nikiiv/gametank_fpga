//============================================================================
//  GameTank audio coprocessor — schematic-true model of the AV board's
//  audio section (AVBoard_smt/signals_4; details in HARDWARE.md §Audio):
//
//   - second 65C02 clocked at CLK14 (14.318 MHz = clk_sys/2) executing from
//     the shared 4 KB dual-port audio RAM, mirrored across its whole
//     address space (vectors included)
//   - AUDIO_RDY (rate register bit 7) is the run gate: it holds the ACP's
//     RDY low when clear (the emulator equivalently never executes ACP
//     cycles unless running)
//   - CD40103 down-counter at 3.579545 MHz: period = {rate[6:0], rate[0]}
//     (P0 and P1 share D0 — literal board wiring), self-reloading; TC is
//     the ACP IRQB (one 3.58 MHz period wide) and the DAC strobe
//   - ACP writes with A15 set stage the byte into the DAC buffer (74573);
//     the AD7524 re-latches that buffer at every tick (zero-order hold)
//   - $2000 write: ACP held in reset and counter forced to $FF; the next
//     TC releases RESB. $2001 write: ACP NMI pulse.
//
//  Bus timing follows the mainbus.sv rule at clk/2: the engine's address is
//  stable only in the non-strobe clock of each 2-clk window, so RAM writes
//  commit there.
//============================================================================

module acp
(
    input  logic        clk_sys,     // 28.636 MHz
    input  logic        reset,

    // register writes (strobe-latched pulses from mainbus)
    input  logic        reg_reset_we,   // $2000
    input  logic        reg_nmi_we,     // $2001
    input  logic        reg_rate_we,    // $2006
    input  logic [7:0]  reg_data,

    // main-CPU window into the shared RAM ($3000-$3FFF)
    input  logic [11:0] win_addr,
    input  logic        win_we,
    input  logic [7:0]  win_din,
    output logic [7:0]  win_dout,

    output logic [7:0]  dac           // current analog output level
);

// ---------------------------------------------------------------------------
// Clock enables
// ---------------------------------------------------------------------------
logic [2:0] div;
always_ff @(posedge clk_sys) begin
    if (reset) div <= '0;
    else       div <= div + 3'd1;
end

wire acp_ce = div[0];                // 14.318 MHz CPU strobe (odd clocks)
wire cnt_ce = (div == 3'd7);         // 3.579545 MHz counter strobe
wire acp_run_ce;                     // gated by AUDIO_RDY, defined below

// ---------------------------------------------------------------------------
// Rate register, sample down-counter, TC tick
// ---------------------------------------------------------------------------
logic [7:0] rate;                    // raw $2006 value; [7] = AUDIO_RDY
logic [7:0] cnt;
logic       tick;                    // 1-clk pulse at TC
logic       rst_pend;                // ACP reset held until next TC

wire [7:0] period = {rate[6:0], rate[0]};

always_ff @(posedge clk_sys) begin
    tick <= 1'b0;
    if (reset) begin
        rate     <= '0;
        cnt      <= 8'hFF;
        rst_pend <= 1'b1;
    end
    else begin
        if (reg_rate_we) rate <= reg_data;
        if (reg_reset_we) begin
            rst_pend <= 1'b1;
            cnt      <= 8'hFF;       // 40103 master reset
        end
        else if (cnt_ce) begin
            if (cnt == 8'd0) begin
                cnt  <= period;
                tick <= 1'b1;
                rst_pend <= 1'b0;    // 7474 CLR: release ACP reset
            end
            else cnt <= cnt - 8'd1;
        end
    end
end

// TC pulse stretched to one 3.58 MHz period (8 clk), as on the board
logic [3:0] tick_len;
wire tick_level = |tick_len;

always_ff @(posedge clk_sys) begin
    if (reset)              tick_len <= '0;
    else if (tick)          tick_len <= 4'd8;
    else if (tick_len != 0) tick_len <= tick_len - 4'd1;
end

// ---------------------------------------------------------------------------
// ACP CPU + shared RAM + DAC
// ---------------------------------------------------------------------------
logic acp_nmi;
always_ff @(posedge clk_sys) begin
    if (reset)           acp_nmi <= 1'b0;
    else if (reg_nmi_we) acp_nmi <= 1'b1;
    else if (acp_ce)     acp_nmi <= 1'b0;   // edge-detected by the core
end

wire [15:0] acp_ab;
wire  [7:0] acp_do;
logic [7:0] acp_di;
wire        acp_we;

// the ACP's 64K space mirrors the 4 KB RAM: only [11:0] and A15 matter
wire _unused = &{1'b0, acp_ab[14:12]};

/* verilator lint_off PINCONNECTEMPTY */
cpu_65c02 acp_cpu
(
    .clk   (clk_sys),
    .reset (reset | rst_pend),
    .AB    (acp_ab),
    .DI    (acp_di),
    .DO    (acp_do),
    .WE    (acp_we),
    .IRQ   (tick_level),
    .NMI   (acp_nmi),
    .RDY   (acp_run_ce),             // AUDIO_RDY run gate
    .SYNC  ()
);
/* verilator lint_on PINCONNECTEMPTY */

// Shared 4 KB dual-port RAM, strobe-latched exactly like mainbus.sv (the
// RDY-gated Arlet bus: transactions sample at the strobe, data is served
// by the next strobe).
assign acp_run_ce = acp_ce && rate[7];   // the CPU only cycles when running

// The Arlet bus is only valid during the RDY strobe (mid-window AB
// derives from stale DIHOLD — see mainbus.sv), so the RAM port can't
// sample the CPU bus every clock: the transaction (addr/data/we) is
// latched at the strobe and the port runs off those registers. The write
// commits one clk after the strobe — still a full clock before the next
// strobe, so the CPU observes identical behavior; the read of the
// strobe-latched address lands in aram_q on that clock, ready for the
// next strobe.
logic [11:0] a_addr;
logic  [7:0] a_din;
logic        a_we;
always_ff @(posedge clk_sys) begin
    if (acp_run_ce) begin
        a_addr <= acp_ab[11:0];
        a_din  <= acp_do;
        a_we   <= acp_we;
    end
    else a_we <= 1'b0;               // write pulse is one clk wide
end

// Both CPUs write, so this is true dual port — and Quartus 17 failed to
// map every behaviorally-correct TDP description of it to M10K
// (clock-enabled port → registers; read-old on a written port →
// "unsupported read-during-write"; bypass mux → "asynchronous read
// logic"). Synthesis therefore instantiates altsyncram directly
// (parameters per sys/sd_card.sv's sdbuf); Verilator keeps the
// behavioral array. The only semantic difference is same-edge
// read-during-write (altsyncram: new data / behavioral: old data),
// which the strobe-latched bus never consumes: DI captured alongside a
// write belongs to a write transaction (ignored), and a read of a
// just-written address is a later transaction that sees the committed
// value on either model.
`ifdef VERILATOR
logic [7:0] aram [0:4095] /*verilator public_flat_rd*/;
logic [7:0] aram_q;
always_ff @(posedge clk_sys) begin
    if (a_we)
        aram[a_addr] <= a_din;
    aram_q <= aram[a_addr];
end
always_ff @(posedge clk_sys) begin
    if (win_we)
        aram[win_addr] <= win_din;
    win_dout <= aram[win_addr];
end
`else
wire [7:0] aram_q;
altsyncram aram
(
    .clock0    (clk_sys),
    .address_a (a_addr),
    .data_a    (a_din),
    .wren_a    (a_we),
    .q_a       (aram_q),

    .clock1    (clk_sys),
    .address_b (win_addr),
    .data_b    (win_din),
    .wren_b    (win_we),
    .q_b       (win_dout),

    .aclr0(1'b0),
    .aclr1(1'b0),
    .addressstall_a(1'b0),
    .addressstall_b(1'b0),
    .byteena_a(1'b1),
    .byteena_b(1'b1),
    .clocken0(1'b1),
    .clocken1(1'b1),
    .clocken2(1'b1),
    .clocken3(1'b1),
    .eccstatus(),
    .rden_a(1'b1),
    .rden_b(1'b1)
);
defparam
    aram.numwords_a = 4096,
    aram.widthad_a  = 12,
    aram.width_a    = 8,
    aram.numwords_b = 4096,
    aram.widthad_b  = 12,
    aram.width_b    = 8,
    aram.address_reg_b = "CLOCK1",
    aram.clock_enable_input_a = "BYPASS",
    aram.clock_enable_input_b = "BYPASS",
    aram.clock_enable_output_a = "BYPASS",
    aram.clock_enable_output_b = "BYPASS",
    aram.indata_reg_b = "CLOCK1",
    aram.intended_device_family = "Cyclone V",
    aram.lpm_type = "altsyncram",
    aram.operation_mode = "BIDIR_DUAL_PORT",
    aram.outdata_aclr_a = "NONE",
    aram.outdata_aclr_b = "NONE",
    aram.outdata_reg_a = "UNREGISTERED",
    aram.outdata_reg_b = "UNREGISTERED",
    aram.power_up_uninitialized = "FALSE",
    aram.read_during_write_mode_port_a = "NEW_DATA_NO_NBE_READ",
    aram.read_during_write_mode_port_b = "NEW_DATA_NO_NBE_READ",
    aram.width_byteena_a = 1,
    aram.width_byteena_b = 1,
    aram.wrcontrol_wraddress_reg_b = "CLOCK1";
`endif
assign acp_di = aram_q;

// DAC: staging buffer on ACP A15 writes; output re-latched at tick
logic [7:0] dac_buf;
always_ff @(posedge clk_sys) begin
    if (reset) begin
        dac_buf <= 8'h80;
        dac     <= 8'h80;
    end
    else begin
        if (acp_run_ce && acp_we && acp_ab[15])
            dac_buf <= acp_do;
        if (tick)
            dac <= dac_buf;
    end
end

endmodule

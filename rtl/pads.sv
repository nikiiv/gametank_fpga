//============================================================================
//  GameTank gamepad ports ($2008 / $2009) — hardware-true model of
//  LogicBoard_smt/Gamepad_ports.kicad_sch (see HARDWARE.md §Gamepads):
//
//   - one 7474 holds both select FFs; reading a pad latches its byte,
//     then (at end of read) toggles that pad's select FF and CLEARS the
//     other pad's — so alternating-read software always starts a pad at
//     select LOW after touching the other port.
//   - byte layout (active low):  D7 = select state as latched BEFORE the
//     toggle, D6 = "extra button" header (unfitted → 1), D5/D4 = Start/A
//     (select LOW) or C/B (select HIGH), D3..D0 = Up/Down/Left/Right
//     (Left/Right only drive in select HIGH; they read 1 in select LOW).
//
//  Note the documented emulator divergence: it returns bits 7/6 always 1.
//  Lockstep carts mask bit 7 before drawing.
//
//  joyN input bit order (MiSTer J1 mapping in GameTank.sv):
//    0=Right 1=Left 2=Down 3=Up 4=A 5=B 6=C 7=Start   (active high)
//============================================================================

module pads
(
    input  logic       clk_sys,
    input  logic       reset,

    input  logic       pad1_rd,   // 1-clk pulses, strobe-aligned (mainbus)
    input  logic       pad2_rd,

    input  logic [7:0] joy1,
    input  logic [7:0] joy2,

    output logic [7:0] pad_q      // data for the most recent pad read
);

logic sel1, sel2;

function automatic logic [7:0] padByte(input logic sel, input logic [7:0] joy);
    return sel ? {1'b1, 1'b1, ~joy[6], ~joy[5], ~joy[3], ~joy[2], ~joy[1], ~joy[0]}
               : {1'b0, 1'b1, ~joy[7], ~joy[4], ~joy[3], ~joy[2], 1'b1,    1'b1};
endfunction

always_ff @(posedge clk_sys) begin
    if (reset) begin
        sel1 <= 1'b0;
        sel2 <= 1'b0;
    end
    else if (pad1_rd) begin
        pad_q <= padByte(sel1, joy1);
        sel1  <= ~sel1;
        sel2  <= 1'b0;
    end
    else if (pad2_rd) begin
        pad_q <= padByte(sel2, joy2);
        sel2  <= ~sel2;
        sel1  <= 1'b0;
    end
end

endmodule

//============================================================================
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but WITHOUT
//  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
//  more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program; if not, write to the Free Software Foundation, Inc.,
//  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
//============================================================================
//
//  GameTank for MiSTer — `emu` wrapper around rtl/gametank.sv.
//  Derived from MiSTer-devel/Template_MiSTer (see docs/DEPENDENCIES.md).
//
//============================================================================

module emu
(
	`include "sys/emu_ports.vh"
);

///////// Default values for ports not used in this core /////////

assign ADC_BUS  = 'Z;
assign USER_OUT = '1;
assign {UART_RTS, UART_TXD, UART_DTR} = 0;
assign {SD_SCK, SD_MOSI, SD_CS} = 'Z;
assign {SDRAM_DQ, SDRAM_A, SDRAM_BA, SDRAM_CLK, SDRAM_CKE, SDRAM_DQML, SDRAM_DQMH, SDRAM_nWE, SDRAM_nCAS, SDRAM_nRAS, SDRAM_nCS} = 'Z;

// DDR3: GRAM lives in HPS DDR3 (rtl/gram_ddr.sv; the cart joins in M7)
assign DDRAM_CLK = clk_sys;

assign VGA_SL = 0;
assign VGA_F1 = 0;
assign VGA_SCALER  = 0;
assign VGA_DISABLE = 0;
assign HDMI_FREEZE = 0;
assign HDMI_BLACKOUT = 0;
assign HDMI_BOB_DEINT = 0;

assign AUDIO_S = 0;
assign AUDIO_MIX = 0;

assign LED_DISK = 0;
assign LED_POWER = 0;
assign BUTTONS = 0;

//////////////////////////////////////////////////////////////////

wire [1:0] ar = status[122:121];

assign VIDEO_ARX = (!ar) ? 12'd4 : (ar - 1'd1);
assign VIDEO_ARY = (!ar) ? 12'd3 : 12'd0;

`include "build_id.v"
localparam CONF_STR = {
	"GameTank;;",
	"-;",
	"F1,GTR,Load Cartridge;",
	"-;",
	"O[122:121],Aspect ratio,Original,Full Screen,[ARC1],[ARC2];",
	"-;",
	"J1,A,B,C,Start;",
	"-;",
	"T[0],Reset;",
	"R[0],Reset and close OSD;",
	"v,0;",
	"V,v",`BUILD_DATE
};

wire forced_scandoubler;
wire   [1:0] buttons;
wire [127:0] status;
wire  [31:0] joystick_0, joystick_1;

hps_io #(.CONF_STR(CONF_STR)) hps_io
(
	.clk_sys(clk_sys),
	.HPS_BUS(HPS_BUS),
	.EXT_BUS(),
	.gamma_bus(),

	.forced_scandoubler(forced_scandoubler),

	.buttons(buttons),
	.status(status),
	.status_menumask(0),

	.joystick_0(joystick_0),
	.joystick_1(joystick_1),

	.ioctl_download(ioctl_download),
	.ioctl_index(ioctl_index),
	.ioctl_wr(ioctl_wr),
	.ioctl_addr(ioctl_addr),
	.ioctl_dout(ioctl_dout),
	.ioctl_wait(ioctl_wait)
);

wire        ioctl_download;
wire [15:0] ioctl_index;
wire        ioctl_wr;
wire [26:0] ioctl_addr;
wire  [7:0] ioctl_dout;
wire        ioctl_wait;

// index 1 = OSD "Load Cartridge"; index 0 = boot.rom auto-load at core start
wire cart_download = ioctl_download && (ioctl_index[5:0] <= 6'd1);

///////////////////////   CLOCKS   ///////////////////////////////

// 28.636364 MHz = 8x NTSC colorburst; the console runs on clock-enables
// derived from it (see rtl/gametank.sv).
wire clk_sys;
pll pll
(
	.refclk(CLK_50M),
	.rst(0),
	.outclk_0(clk_sys)
);

// Hold the console in reset for the whole cart transfer; the cart's
// download machinery runs through reset by design (rtl/cart.sv).
wire reset = RESET | status[0] | buttons[1] | cart_download;

///////////////////////   CORE   /////////////////////////////////

wire HBlank;
wire HSync;
wire VBlank;
wire VSync;
wire ce_pix;
wire [7:0] video_r, video_g, video_b;

// Built-in boot cart: sim/testroms/pads_demo.gtr baked into BRAM so the
// video chain is demonstrable on hardware before OSD .gtr loading (M7).
// Regenerate with: hexdump -v -e '1/1 "%02X\n"' sim/testroms/pads_demo.gtr > rtl/bootcart.mem
logic [7:0] bootcart [0:32767];
initial $readmemh("rtl/bootcart.mem", bootcart);

wire [14:0] cart_addr;
logic [7:0] cart_data;
always @(posedge clk_sys) cart_data <= bootcart[cart_addr];

gametank gametank
(
	.clk_sys (clk_sys),
	.reset   (reset),

	.cart_addr (cart_addr),
	.cart_data (cart_data),

	.dl_active (cart_download),
	.dl_wr     (ioctl_wr && cart_download),
	.dl_addr   (ioctl_addr[20:0]),
	.dl_data   (ioctl_dout),
	.dl_busy   (ioctl_wait),

	.ddr_rd         (DDRAM_RD),
	.ddr_we         (DDRAM_WE),
	.ddr_addr       (DDRAM_ADDR),
	.ddr_din        (DDRAM_DIN),
	.ddr_be         (DDRAM_BE),
	.ddr_burstcnt   (DDRAM_BURSTCNT),
	.ddr_dout       (DDRAM_DOUT),
	.ddr_dout_ready (DDRAM_DOUT_READY),
	.ddr_busy       (DDRAM_BUSY),

	.joy1 (joystick_0[7:0]),
	.joy2 (joystick_1[7:0]),

	.ce_pix  (ce_pix),
	.hblank  (HBlank),
	.hsync   (HSync),
	.vblank  (VBlank),
	.vsync   (VSync),
	.video_r (video_r),
	.video_g (video_g),
	.video_b (video_b),

	.audio_l (AUDIO_L),
	.audio_r (AUDIO_R)
);

assign CLK_VIDEO = clk_sys;
assign CE_PIXEL = ce_pix;

assign VGA_DE = ~(HBlank | VBlank);
assign VGA_HS = HSync;
assign VGA_VS = VSync;
assign VGA_R  = video_r;
assign VGA_G  = video_g;
assign VGA_B  = video_b;

reg  [26:0] act_cnt;
always @(posedge clk_sys) act_cnt <= act_cnt + 1'd1;
assign LED_USER = act_cnt[26] ? act_cnt[25:18] > act_cnt[7:0] : act_cnt[25:18] <= act_cnt[7:0];

endmodule

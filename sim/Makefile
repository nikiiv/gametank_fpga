# sim/Makefile — Verilator test suites for the GameTank core.
# Invoked by tools/gametank-test (and CI). Targets: unit, integration, system.
#
# The sim top for system-level tests is rtl/gametank.sv itself — sys/ is never
# simulated. CPU import-gate tests verilate the vendored core alone. Each test
# is a standalone C++ program on top of the headers in common/.

VERILATOR ?= verilator

RTL_DIR := $(abspath ../rtl)
COMMON  := $(abspath common)
OUT     := out

CPU_SRCS := $(RTL_DIR)/cpu/cpu_65c02.v $(RTL_DIR)/cpu/ALU.v
RTL_SRCS := $(abspath waivers.vlt) $(RTL_DIR)/gametank.sv $(RTL_DIR)/gtvideo.sv \
            $(RTL_DIR)/mainbus.sv $(RTL_DIR)/vram.sv $(RTL_DIR)/palette.sv $(CPU_SRCS)

# Our own RTL must stay -Wall clean; the vendored CPU is exempt (policy:
# vendored cores are not linted/fixed), so its builds are -Wno-fatal.
VFLAGS     := --cc --exe --build -j 0 -Wall --assert -O2 --top-module gametank
VFLAGS_CPU := --cc --exe --build -j 0 -Wno-fatal --x-assign unique --x-initial unique -O2 --top-module cpu_65c02
CFLAGS     := -std=c++17 -O2 -I$(COMMON)

# ---------------------------------------------------------------------------
# Test registry. Pattern-rule tests build out/<tier>_<name> from
# <tier>/test_<name>.cpp against RTL_SRCS. Klaus import-gate tests have
# explicit rules below (extra fetch/build inputs and arguments).
# ---------------------------------------------------------------------------
UNIT_TESTS        := video_timing mainbus vram_window cpu_klaus_6502 cpu_klaus_65c02 cpu_wai_stp
INTEGRATION_TESTS := cart_ram_pattern cart_fb_pattern

# gametank-test --test NAME passes TEST=NAME
TEST ?=
ifneq ($(TEST),)
UNIT_TESTS        := $(filter $(TEST),$(UNIT_TESTS))
INTEGRATION_TESTS := $(filter $(TEST),$(INTEGRATION_TESTS))
endif

.PHONY: unit integration system clean
.SECONDARY:  # keep built test binaries between runs

unit: $(UNIT_TESTS:%=run-unit-%)

integration: $(INTEGRATION_TESTS:%=run-integration-%)

system:
	@echo "No system tests yet (the lockstep harness arrives in M4)."

run-unit-%: $(OUT)/unit_%
	@echo ">>> unit/$*"
	@cd $(OUT) && ./unit_$*

run-integration-%: $(OUT)/integration_%
	@echo ">>> integration/$*"
	@cd $(OUT) && ./integration_$*

$(OUT)/unit_%: unit/test_%.cpp common/sim_harness.h $(RTL_SRCS) | $(OUT)
	@mkdir -p obj_dir/unit_$*
	$(VERILATOR) $(VFLAGS) --Mdir obj_dir/unit_$* -CFLAGS "$(CFLAGS)" \
		-o $(abspath $@) $(RTL_SRCS) $(abspath $<)

$(OUT)/integration_%: integration/test_%.cpp common/sim_harness.h $(RTL_SRCS) | $(OUT)
	@mkdir -p obj_dir/integration_$*
	$(VERILATOR) $(VFLAGS) --Mdir obj_dir/integration_$* -CFLAGS "$(CFLAGS)" \
		-o $(abspath $@) $(RTL_SRCS) $(abspath $<)

$(OUT):
	mkdir -p $(OUT)

# ---------------------------------------------------------------------------
# Test carts (ca65/ld65 from the cc65 suite)
# ---------------------------------------------------------------------------
run-integration-cart_ram_pattern: testroms/ram_pattern.gtr

testroms/ram_pattern.gtr: testroms/ram_pattern/ram_pattern.s testroms/ram_pattern/gametank.cfg
	cd testroms/ram_pattern && \
	  ca65 ram_pattern.s -o ram_pattern.o && \
	  ld65 ram_pattern.o -C gametank.cfg -o ../ram_pattern.gtr

clean:
	rm -rf obj_dir $(OUT)

# ---------------------------------------------------------------------------
# Klaus Dormann import gate (one-time gate on the vendored CPU, TESTING.md).
# Test material is cloned on demand at pinned commits — never vendored.
# ---------------------------------------------------------------------------
KLAUS_URL    := https://github.com/Klaus2m5/6502_65C02_functional_tests
KLAUS_COMMIT := 7954e2dbb49c469ea286070bf46cdd71aeb29e4b
AMB5L_URL    := https://github.com/amb5l/6502_65C02_functional_tests
AMB5L_COMMIT := 966b1a35049f9d8be44ad092ec6d43d5ba1831b3

klaus/upstream/.stamp:
	rm -rf klaus/upstream
	git clone -q $(KLAUS_URL) klaus/upstream
	cd klaus/upstream && git checkout -q $(KLAUS_COMMIT)
	touch $@

klaus/ca65port/.stamp:
	rm -rf klaus/ca65port
	git clone -q $(AMB5L_URL) klaus/ca65port
	cd klaus/ca65port && git checkout -q $(AMB5L_COMMIT)
	touch $@

# 65C02 extended test, reassembled per docs/DEPENDENCIES.md: Rockwell ops OFF
# (they run as NOPs in our core), WDC WAI/STP skipped here (they get directed
# tests instead), and a label planted on the success trap so the testbench
# can detect it exactly.
klaus/65C02_extended_opcodes_test.bin: klaus/ca65port/.stamp
	cd klaus/ca65port/ca65 && \
	  sed -e 's/^rkwl_wdc_op = 1/rkwl_wdc_op = 0/' \
	      -e 's/^        success         ;if you get here everything went well/success_pc: success ;(label planted by sim build)/' \
	      65C02_extended_opcodes_test.ca65 > extended_gt.ca65 && \
	  ca65 -g -l extended_gt.lst extended_gt.ca65 && \
	  ld65 extended_gt.o -o $(abspath $@) -m extended_gt.map -Ln extended_gt.lbl -C example.cfg

klaus/extended_success_pc.txt: klaus/65C02_extended_opcodes_test.bin
	grep -i '\.success_pc$$' klaus/ca65port/ca65/extended_gt.lbl \
	  | awk '{print $$2}' | sed 's/^00//' > $@
	@test -s $@ || { echo "success_pc label not found"; rm -f $@; exit 1; }

$(OUT)/unit_cpu_klaus: unit/test_cpu_klaus.cpp common/cpu65_harness.h $(CPU_SRCS) | $(OUT)
	@mkdir -p obj_dir/unit_cpu_klaus
	$(VERILATOR) $(VFLAGS_CPU) --Mdir obj_dir/unit_cpu_klaus -CFLAGS "$(CFLAGS)" \
		-o $(abspath $@) $(CPU_SRCS) $(abspath $<)

$(OUT)/unit_cpu_wai_stp: unit/test_cpu_wai_stp.cpp common/cpu65_harness.h $(CPU_SRCS) | $(OUT)
	@mkdir -p obj_dir/unit_cpu_wai_stp
	$(VERILATOR) $(VFLAGS_CPU) --Mdir obj_dir/unit_cpu_wai_stp -CFLAGS "$(CFLAGS)" \
		-o $(abspath $@) $(CPU_SRCS) $(abspath $<)

run-unit-cpu_klaus_6502: $(OUT)/unit_cpu_klaus klaus/upstream/.stamp
	@echo ">>> unit/cpu_klaus_6502"
	@cd $(OUT) && ./unit_cpu_klaus ../klaus/upstream/bin_files/6502_functional_test.bin 3469

run-unit-cpu_klaus_65c02: $(OUT)/unit_cpu_klaus klaus/65C02_extended_opcodes_test.bin klaus/extended_success_pc.txt
	@echo ">>> unit/cpu_klaus_65c02"
	@cd $(OUT) && ./unit_cpu_klaus ../klaus/65C02_extended_opcodes_test.bin $$(cat ../klaus/extended_success_pc.txt)

run-integration-cart_fb_pattern: testroms/fb_pattern.gtr

testroms/fb_pattern.gtr: testroms/fb_pattern/fb_pattern.s testroms/fb_pattern/gametank.cfg
	cd testroms/fb_pattern && \
	  ca65 fb_pattern.s -o fb_pattern.o && \
	  ld65 fb_pattern.o -C gametank.cfg -o ../fb_pattern.gtr

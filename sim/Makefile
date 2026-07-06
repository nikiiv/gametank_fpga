# sim/Makefile — Verilator test suites for the GameTank core.
# Invoked by tools/gametank-test (and CI). Targets: unit, integration, system.
#
# The sim top is rtl/gametank.sv itself — sys/ is never simulated. Each test
# is a standalone C++ program on top of common/sim_harness.h.

VERILATOR ?= verilator

RTL_DIR := $(abspath ../rtl)
COMMON  := $(abspath common)
OUT     := out

RTL_SRCS := $(RTL_DIR)/gametank.sv $(RTL_DIR)/testpattern.sv

VFLAGS := --cc --exe --build -j 0 -Wall --assert -O2 --top-module gametank
CFLAGS := -std=c++17 -O2 -I$(COMMON)

UNIT_TESTS        := video_timing
INTEGRATION_TESTS := frame_capture

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

clean:
	rm -rf obj_dir $(OUT)

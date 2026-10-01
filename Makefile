# HEADCRASH Format Util
# (C) 2026 Robert Mech. Licence GPL-3.0-or-later.
#
# make           build build/headcrash.prg
# make tests     build the test harnesses
# make clean     remove build output
#
# Needs cc65 (tested with 2.19) and Python 3. The test targets additionally
# need VICE 3.10 with the headless UI, for x64sc and c1541.

CC65    := cl65
TARGET  := c64
CFLAGS  := -t $(TARGET) -O -I src

BUILD   := build
SRC     := src/main.c src/ui.c src/dos.c src/fmt.c
ASM     := src/gfx.s
GEN     := src/drivecode_1541.h

PRG     := $(BUILD)/headcrash.prg

HARNESS := t_probe t_fmt t_full t_sect

.PHONY: all tests clean

all: $(PRG)

# The gate that runs inside the 1541 is assembled from source and turned
# into C data, so there is one source of truth and no hand typed opcodes.
$(GEN): src/drivecode_1541.s tools/drivecode.cfg tools/build_drivecode.py
	@mkdir -p $(BUILD)
	python3 tools/build_drivecode.py

$(PRG): $(SRC) $(ASM) $(GEN) src/dos.h src/fmt.h src/ui.h
	@mkdir -p $(BUILD)
	$(CC65) $(CFLAGS) -o $@ $(SRC) $(ASM)

tests: $(GEN)
	@mkdir -p $(BUILD)
	$(foreach h,$(HARNESS),$(CC65) $(CFLAGS) -o $(BUILD)/$(h).prg \
		tests/$(h).c src/dos.c src/fmt.c &&) true

clean:
	rm -rf $(BUILD)

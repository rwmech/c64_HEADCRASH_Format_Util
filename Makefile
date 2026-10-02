# HEADCRASH Format Util
# (C) 2026 Robert Mech. Licence MIT.
#
# make           build build/headcrash.prg
# make tests     build the test harnesses
# make clean     remove build output
#
# Needs cc65 (tested with 2.19) and Python 3. The test targets additionally
# need VICE 3.10 with the headless UI, for x64sc and c1541.

CC65    := cl65
TARGET  := c64

# The VIC is put in bank 1, with everything it reads packed against the top
# of that bank: sprites at $5000, character set at $5400, screen matrix at
# $5c00, bitmap at $6000. That keeps it all as plain RAM with the ROMs
# mapped in, so drawing never banks anything out, and leaves the program
# and its stack everything below $5B00.
#
# cfg/headcrash.cfg adds a second code region in the free RAM at $C9D1 and
# gives BSS a size the linker can actually check. The stock c64.cfg works
# BSS out by subtraction, so a program that outgrows its space wraps the
# size instead of failing, links without a word, and draws garbage.
CFLAGS  := -t $(TARGET) -O -I src -C cfg/headcrash.cfg \
           -Wl -D__HIMEM__=0x5b00 -Wl -D__STACKSIZE__=0x400

BUILD   := build
SRC     := src/main.c src/ui.c src/dos.c src/fmt.c src/snd.c
ASM     := src/gfx.s
GEN     := src/drivecode_1541.h

PRG     := $(BUILD)/headcrash.prg

HARNESS := t_probe t_fmt t_full t_sect t_delay

.PHONY: all tests dist clean

all: $(PRG)

# The gate that runs inside the 1541 is assembled from source and turned
# into C data, so there is one source of truth and no hand typed opcodes.
$(GEN): src/drivecode_1541.s tools/drivecode.cfg tools/build_drivecode.py
	@mkdir -p $(BUILD)
	python3 tools/build_drivecode.py

$(PRG): $(SRC) $(ASM) $(GEN) src/dos.h src/fmt.h src/ui.h
	@mkdir -p $(BUILD)
	$(CC65) $(CFLAGS) -o $@ $(SRC) $(ASM)

# Same program, but it starts formatting without being asked, so a run can
# be driven from the command line under an emulator.
$(BUILD)/hc_auto.prg: $(SRC) $(ASM) $(GEN)
	@mkdir -p $(BUILD)
	$(CC65) $(CFLAGS) -DAUTORUN -o $@ $(SRC) $(ASM)

tests: $(GEN) $(BUILD)/hc_auto.prg
	@mkdir -p $(BUILD)
	$(foreach h,$(HARNESS),$(CC65) $(CFLAGS) -o $(BUILD)/$(h).prg \
		tests/$(h).c src/dos.c src/fmt.c &&) true

# The binaries that ship in dist/, so the repository carries something that
# runs without a toolchain. c1541 comes with VICE.
dist: $(PRG)
	@mkdir -p dist
	cp $(PRG) dist/headcrash.prg
	rm -f dist/headcrash.d64 dist/headcrash.d81
	c1541 -format "headcrash,hc" d64 dist/headcrash.d64 \
		-write $(PRG) "headcrash"
	c1541 -format "headcrash,hc" d81 dist/headcrash.d81 \
		-write $(PRG) "headcrash"

clean:
	rm -rf $(BUILD)

# Makefile to help with assembly

OUTDIR=out
SRCDIR=examples
MKDIR=@mkdir
PYTHON=python3
TAS=$(PYTHON) tas.py
TASFLAGS=--include assembler.inc --include aliases.ymp
EMUDIR=../emu/
EMUEXE=tern-cpu-emu
EMU=$(EMUDIR)$(EMUEXE)
EMUDEPS=$(wildcard $(EMUDIR)*.c) $(wildcard $(EMUDIR)*.h)
TASFILES=$(wildcard $(SRCDIR)/*.tas)
LIBFILES=$(wildcard lib/*.tas)
TRTFILES=$(TASFILES:$(SRCDIR)/%.tas=$(OUTDIR)/%.trt)
BENCH_RUNS?=5
BENCH_LIMIT?=1000000
BENCH_EMU=$(EMUDIR)bench-guest

default: test

$(OUTDIR):
	$(MKDIR) $(OUTDIR)

# Rule to assemble Ternary Assembly into loadable Trit files
$(OUTDIR)/%.trt: $(SRCDIR)/%.tas tas.py assembler.inc aliases.ymp $(LIBFILES) | $(OUTDIR)
	$(TAS) $(TASFLAGS) $< -o $@

$(OUTDIR)/bench-%.trt: benchmarks/%.tas tas.py assembler.inc aliases.ymp $(LIBFILES) | $(OUTDIR)
	$(TAS) $(TASFLAGS) $< -o $@

$(BENCH_EMU): $(EMUDEPS) $(EMUDIR)tests/bench_guest.c
	$(MAKE) -C $(EMUDIR) bench-guest

bench: $(OUTDIR)/bench-memory.trt $(OUTDIR)/bench-math.trt $(BENCH_EMU)
	$(BENCH_EMU) $(OUTDIR)/bench-memory.trt 2392578 $(BENCH_RUNS) $(BENCH_LIMIT)
	$(BENCH_EMU) $(OUTDIR)/bench-math.trt 486 $(BENCH_RUNS) $(BENCH_LIMIT)

# Rule to run a Trit file in the emulator
%: $(OUTDIR)/%.trt $(EMU)
	$(EMU) $<

all: $(TRTFILES)

test: all $(EMU)
	$(PYTHON) tests/smoke.py

clean:
	rm -rf $(OUTDIR)

clean-all: clean

$(EMU): $(EMUDEPS)
	$(MAKE) -C $(EMUDIR) $(EMUEXE)

.PRECIOUS: $(OUTDIR)/%.trt

.PHONY: default all test bench clean clean-all

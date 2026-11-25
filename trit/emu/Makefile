# Quick Makefile

PKG=tern-cpu-emu
GUEST=tern-linux
SRCEXT=.c
SRCS=$(filter-out rv32.c,$(wildcard *$(SRCEXT)))
STRIP=strip
CC=gcc
CFLAGS=-Wall -Ofast
RV32_CFLAGS=-Wall -O1 -g -Wno-unused-function -fno-fast-math -frounding-math -fsignaling-nans
LIBS=-lm
RV32_LIBS=$(LIBS) -lquadmath
#PREFIX=/usr/local
#CONFIGDIR=/etc
DEPDIR=deps
DEPS=$(SRCS:%$(SRCEXT)=$(DEPDIR)/%.d)
OBJDIR=obj
OBJS=$(SRCS:%$(SRCEXT)=$(OBJDIR)/%.o)
ASMDIR=asm
ASMS=$(SRCS:%$(SRCEXT)=$(ASMDIR)/%.s)
DOCDIR=doc
DOCCMD=doxygen
DOCCFG=Doxyfile
SED=sed
MKDIR=@mkdir

$(PKG): $(OBJS)
	@echo "Generating $@"
	$(CC) $(LIBS) $(CFLAGS) $(OBJS) -o $(PKG)
	$(STRIP) $(PKG)

pkg: $(PKG) $(GUEST)

linux: $(GUEST)

$(GUEST): rv32.c
	@echo "Generating $@"
	$(CC) $(RV32_CFLAGS) rv32.c -o $@ $(RV32_LIBS)
	$(STRIP) $@


tern-test: ternary.c ternary.h
	@echo "Generating $@"
	$(CC) $(LIBS) $(CFLAGS) ternary.c -DTERNARY_MATH_MAIN=1 -o $@
	$(STRIP) $@


$(OBJDIR):
	$(MKDIR) $(OBJDIR)

$(OBJDIR)/%.o: %$(SRCEXT) | $(OBJDIR)
	@echo "CC $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(ASMDIR):
	$(MKDIR) $(ASMDIR)

$(ASMDIR)/%.s: %$(SRCEXT) | $(ASMDIR)
	$(CC) $(CFLAGS) -S -fverbose-asm -c $< -o $@

$(DEPDIR):
	$(MKDIR) $(DEPDIR)

$(DEPDIR)/%.d: %$(SRCEXT) | $(DEPDIR)
	$(CC) $(CFLAGS) -c $< -MM -o $@
	$(SED) -i 's+\($*\)\.o[ :]*+$(OBJDIR)/\1.o $@ : +g' $@

asms: $(ASMS)


CORE_SRCS=cpu.c mem.c disassem.c ternary.c tml.c tml_log.c tml_policy.c sha256.c trace.c taint.c symbolic.c
TESTBIN=test-core
TMLTESTBIN=test-tml
PHASE2TESTBIN=test-phase2
RV32TESTBIN=test-rv32-mmu
RV32CSRTESTBIN=test-rv32-csr
RV32TRAPTESTBIN=test-rv32-trap
RV32TLBTESTBIN=test-rv32-tlb
RV32AMOTESTBIN=test-rv32-amo
RV32RVCTESTBIN=test-rv32-rvc
RV32FPTESTBIN=test-rv32-fpu
RV32FUZZTESTBIN=test-rv32-fuzz
TEST_SRCS=tests/test_core.c $(CORE_SRCS)
TML_TEST_SRCS=tests/test_tml.c $(CORE_SRCS)
PHASE2_TEST_SRCS=tests/test_phase2.c $(CORE_SRCS)
BENCH_SRCS=tests/bench_cpu.c $(CORE_SRCS)
BENCH_HEADERS=$(wildcard *.h)
BENCH_CYCLES?=1000000
BENCH_FPU_OPS?=5000000
RV32BENCHBIN=bench-rv32-fpu-bin

bench-cpu: $(BENCH_SRCS) $(BENCH_HEADERS)
	$(CC) $(CFLAGS) $(BENCH_SRCS) -o $@

bench-cpu-uncached: $(BENCH_SRCS) $(BENCH_HEADERS)
	$(CC) $(CFLAGS) -DTERNARY_DECODE_CACHE=0 $(BENCH_SRCS) -o $@

bench-guest: tests/bench_guest.c $(CORE_SRCS) $(BENCH_HEADERS)
	$(CC) $(CFLAGS) tests/bench_guest.c $(CORE_SRCS) -o $@

.PHONY: bench
bench: bench-cpu bench-cpu-uncached
	@echo "Decode cache disabled:"
	./bench-cpu-uncached $(BENCH_CYCLES)
	@echo "Decode cache enabled:"
	./bench-cpu $(BENCH_CYCLES)

$(RV32BENCHBIN): tests/bench_rv32_fpu.c rv32.c
	@echo "Generating $@"
	$(CC) $(RV32_CFLAGS) tests/bench_rv32_fpu.c -o $@ $(RV32_LIBS)

.PHONY: bench-rv32-fpu
bench-rv32-fpu: $(RV32BENCHBIN)
	./$(RV32BENCHBIN) $(BENCH_FPU_OPS)

$(TESTBIN): $(TEST_SRCS) cpu.h mem.h ternary.h disassem.h tests/test_core.c
	@echo "Generating $@"
	$(CC) $(CFLAGS) $(TEST_SRCS) -o $@

$(TMLTESTBIN): $(TML_TEST_SRCS) cpu.h mem.h ternary.h tml.h tml_policy.h tests/test_tml.c
	@echo "Generating $@"
	$(CC) $(CFLAGS) $(TML_TEST_SRCS) -o $@

$(PHASE2TESTBIN): $(PHASE2_TEST_SRCS) cpu.h mem.h trace.h taint.h symbolic.h tests/test_phase2.c
	@echo "Generating $@"
	$(CC) $(CFLAGS) $(PHASE2_TEST_SRCS) -o $@

test: $(TESTBIN) $(TMLTESTBIN) $(PHASE2TESTBIN)
	./$(TESTBIN)
	./$(TMLTESTBIN)
	./$(PHASE2TESTBIN)

$(RV32TESTBIN): tests/test_rv32_mmu.c rv32.c
	@echo "Generating $@"
	$(CC) $(RV32_CFLAGS) tests/test_rv32_mmu.c -o $@ $(RV32_LIBS)

$(RV32CSRTESTBIN): tests/test_rv32_csr.c rv32.c
	@echo "Generating $@"
	$(CC) $(RV32_CFLAGS) tests/test_rv32_csr.c -o $@ $(RV32_LIBS)

$(RV32TRAPTESTBIN): tests/test_rv32_trap.c rv32.c
	@echo "Generating $@"
	$(CC) $(RV32_CFLAGS) tests/test_rv32_trap.c -o $@ $(RV32_LIBS)

$(RV32TLBTESTBIN): tests/test_rv32_tlb.c rv32.c
	@echo "Generating $@"
	$(CC) $(RV32_CFLAGS) tests/test_rv32_tlb.c -o $@ $(RV32_LIBS)

$(RV32AMOTESTBIN): tests/test_rv32_amo.c rv32.c
	@echo "Generating $@"
	$(CC) $(RV32_CFLAGS) tests/test_rv32_amo.c -o $@ $(RV32_LIBS)

$(RV32RVCTESTBIN): tests/test_rv32_rvc.c rv32.c
	@echo "Generating $@"
	$(CC) $(RV32_CFLAGS) tests/test_rv32_rvc.c -o $@ $(RV32_LIBS)

$(RV32FPTESTBIN): tests/test_rv32_fpu.c rv32.c
	@echo "Generating $@"
	$(CC) $(RV32_CFLAGS) tests/test_rv32_fpu.c -o $@ $(RV32_LIBS)

$(RV32FUZZTESTBIN): tests/test_rv32_fuzz.c rv32.c
	@echo "Generating $@"
	$(CC) $(RV32_CFLAGS) tests/test_rv32_fuzz.c -o $@ $(RV32_LIBS)

.PHONY: test-rv32
test-rv32: $(RV32TESTBIN) $(RV32CSRTESTBIN) $(RV32TRAPTESTBIN) $(RV32TLBTESTBIN) $(RV32AMOTESTBIN) $(RV32RVCTESTBIN) $(RV32FPTESTBIN) $(RV32FUZZTESTBIN) $(GUEST)
	./$(RV32TESTBIN)
	./$(RV32CSRTESTBIN)
	./$(RV32TRAPTESTBIN)
	./$(RV32TLBTESTBIN)
	./$(RV32AMOTESTBIN)
	./$(RV32RVCTESTBIN)
	./$(RV32FPTESTBIN)
	./$(RV32FUZZTESTBIN)
	python3 tests/test_rv32_images.py ./$(GUEST)

RV32_CROSS?=riscv64-unknown-elf-
RV32_GUEST_SRC=tests/rv32_mmu_guest.S tests/rv32_mmu_guest.ld
RV32_GUEST_ELF=tests/rv32_mmu_guest.elf
RV32_GUEST_BIN=tests/rv32_mmu_guest.bin

$(RV32_GUEST_ELF): $(RV32_GUEST_SRC)
	$(RV32_CROSS)gcc -march=rv32ima_zicsr_zifencei -mabi=ilp32 -nostdlib \
		-static -mno-relax -Wl,-T,tests/rv32_mmu_guest.ld \
		-o $@ tests/rv32_mmu_guest.S

$(RV32_GUEST_BIN): $(RV32_GUEST_ELF)
	$(RV32_CROSS)objcopy -O binary $< $@

.PHONY: test-rv32-guest
test-rv32-guest: $(GUEST) $(RV32_GUEST_BIN)
	./$(GUEST) --kernel $(RV32_GUEST_BIN) --max-insn 1000000 --quiet

.PHONY: test-tools
test-tools: $(PKG)
	python3 -m unittest discover -s ../tools/tests -v

sanitize:
	$(CC) -g -O1 -Wall -Wextra -fsanitize=address,undefined \
		$(TEST_SRCS) -o $(TESTBIN)-san
	$(CC) -g -O1 -Wall -Wextra -fsanitize=address,undefined \
		$(TML_TEST_SRCS) -o $(TMLTESTBIN)-san
	$(CC) -g -O1 -Wall -Wextra -fsanitize=address,undefined \
		$(PHASE2_TEST_SRCS) -o $(PHASE2TESTBIN)-san
	./$(TESTBIN)-san
	./$(TMLTESTBIN)-san
	./$(PHASE2TESTBIN)-san

#install: $(PKG)
#	cp $(PKG) $(PREFIX)/bin/
#	cp $(PKG)rc $(CONFIGDIR)/

clean-asm:
	rm -rf $(ASMDIR)

clean-obj:
	rm -rf $(OBJDIR)

clean-dep:
	rm -rf $(DEPDIR)

clean: clean-obj clean-asm
	rm -rf *~

clean-pkg: clean clean-dep
	rm -rf $(PKG) $(GUEST) tern-test $(TESTBIN) $(TESTBIN)-san $(TMLTESTBIN) $(TMLTESTBIN)-san $(PHASE2TESTBIN) $(PHASE2TESTBIN)-san $(RV32TESTBIN) $(RV32CSRTESTBIN) $(RV32TRAPTESTBIN) $(RV32TLBTESTBIN) $(RV32AMOTESTBIN) $(RV32RVCTESTBIN) $(RV32FPTESTBIN) $(RV32BENCHBIN) bench-cpu bench-cpu-uncached bench-guest

rebuild: clean-pkg pkg

$(DOCCFG):
	$(DOCCMD) -g $(DOCCFG)
	$(SED) -i 's+^OUTPUT_DIRECTORY *=.*$$+OUTPUT_DIRECTORY       = $(DOCDIR)+g' $(DOCCFG)
	$(SED) -i 's+^PROJECT_NAME *=.*$$+PROJECT_NAME           = "$(PKG)"+g' $(DOCCFG)
	$(SED) -i 's+^GENERATE_LATEX *=.*$$+GENERATE_LATEX         = NO+g' $(DOCCFG)
	$(SED) -i 's+^SOURCE_BROWSER *=.*$$+SOURCE_BROWSER         = YES+g' $(DOCCFG)
	$(SED) -i 's+^GENERATE_TREEVIEW *=.*$$+GENERATE_TREEVIEW      = YES+g' $(DOCCFG)
#	$(SED) -i 's+^PLANTUML_JAR_PATH *=.*$$+PLANTUML_JAR_PATH      = /usr/share/plantuml/plantuml.jar+g' $(DOCCFG)

docs: $(DOCCFG)
	$(DOCCMD) $(DOCCFG)

clean-docs:
	rm -rf $(DOCDIR)

rebuild-docs: clean-docs docs

all: $(PKG) docs

clean-all: clean-pkg clean-docs

rebuild-all: clean-all all

include $(DEPS)

#gcc -Wall -Os -fgcse-after-reload -fipa-cp-clone -floop-interchange -floop-unroll-and-jam -fpeel-loops -fpredictive-commoning -fsplit-loops -fsplit-paths -ftree-loop-distribution -ftree-loop-vectorize -ftree-partial-pre -ftree-slp-vectorize -funswitch-loops -fvect-cost-model -fvect-cost-model=dynamic -fversion-loops-for-strides main.c

#gcc -Wall -Os -fassociative-math -fcx-limited-range -ffinite-math-only -fgcse-after-reload -fipa-cp-clone -floop-interchange -floop-unroll-and-jam -foptimize-strlen -fpeel-loops -fpredictive-commoning -freciprocal-math -fsplit-loops -fsplit-paths -ftree-loop-distribute-patterns -ftree-loop-distribution -ftree-loop-vectorize -ftree-partial-pre -ftree-slp-vectorize -funsafe-math-optimizations -funswitch-loops -fno-math-errno -fno-signed-zeros -fno-trapping-math -fvect-cost-model=dynamic main.c

$(V).SILENT:

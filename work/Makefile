# SPDX-License-Identifier: GPL-2.0-only
WORK := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO := $(abspath $(WORK)/..)
SCRIPTS := $(WORK)pipelines/scripts
TERNARY_REPO ?= $(HOME)/System/ternary
export TERNARY_REPO

.PHONY: help deps build machine checkpatch sparse kunit security-kunit smoke smoke-kasan lint metrics ci-check ternary-test ternary-bench ternary-coverage ternary-metrics ternary-ci ternary-linux-deps ternary-linux-check ternary-linux-build ternary-linux-boot ternary-linux-userspace ternary-linux-run ternary-linux-isa clean

help:
	@echo "Targets:"
	@echo "  deps             install build dependencies"
	@echo "  build            allmodconfig build"
	@echo "  machine          build 7.2.6 for this machine"
	@echo "  checkpatch       run checkpatch against BASE (default origin/master)"
	@echo "  sparse           run sparse on defconfig"
	@echo "  kunit            run KUnit (UML)"
	@echo "  security-kunit   run security KUnit suites (UML)"
	@echo "  smoke            boot defconfig kernel in QEMU"
	@echo "  smoke-kasan      boot KASAN/KFENCE/UBSAN kernel in QEMU"
	@echo "  lint             run bash/shellcheck/actionlint checks"
	@echo "  metrics          regenerate work/spec/metrics.md"
	@echo "  ci-check         lint plus metrics freshness check"
	@echo "  ternary-test     run baseline/candidate ternary C tests"
	@echo "  ternary-bench    run baseline/candidate ternary benchmarks"
	@echo "  ternary-coverage collect baseline/candidate line/branch coverage"
	@echo "  ternary-metrics  run ternary tests+bench+coverage and gate"
	@echo "  ternary-ci       alias for ternary-metrics"
	@echo "  ternary-linux-deps   install RV32 toolchain, QEMU, dtc, OpenSBI"
	@echo "  ternary-linux-check  verify RV32 Linux guest prerequisites"
	@echo "  ternary-linux-build  build RV32 NOMMU guest kernel"
	@echo "  ternary-linux-boot   boot guest kernel in QEMU"
	@echo "  ternary-linux-userspace  build flat /init and initramfs"
	@echo "  ternary-linux-run    boot guest under ternary emulator"
	@echo "  ternary-linux-isa    run RV32IMA/SBI emulator regression tests"
	@echo "  clean            remove $(WORK)build"

deps:
	$(SCRIPTS)/deps.sh

build:
	$(SCRIPTS)/build.sh

machine:
	$(SCRIPTS)/build-machine.sh

checkpatch:
	$(SCRIPTS)/checkpatch.sh

sparse:
	$(SCRIPTS)/sparse.sh

kunit:
	$(SCRIPTS)/kunit.sh

security-kunit:
	$(SCRIPTS)/security-kunit.sh

smoke:
	$(SCRIPTS)/smoke-qemu.sh

smoke-kasan:
	$(SCRIPTS)/smoke-kasan.sh

lint:
	$(SCRIPTS)/lint.sh

metrics:
	$(SCRIPTS)/metrics.sh --write

ci-check:
	$(SCRIPTS)/lint.sh
	$(SCRIPTS)/metrics.sh --check

ternary-test:
	$(SCRIPTS)/ternary-test.sh all

ternary-bench:
	$(SCRIPTS)/ternary-bench.sh all

ternary-coverage:
	$(SCRIPTS)/ternary-coverage.sh all

ternary-metrics:
	$(SCRIPTS)/ternary-metrics.sh --check

ternary-ci: ternary-metrics

ternary-linux-deps:
	$(SCRIPTS)/ternary-linux-deps.sh

ternary-linux-check:
	$(SCRIPTS)/ternary-linux-check.sh

ternary-linux-build:
	$(SCRIPTS)/ternary-linux-build.sh

ternary-linux-boot:
	$(SCRIPTS)/ternary-linux-boot.sh

ternary-linux-userspace:
	$(SCRIPTS)/ternary-linux-userspace.sh

ternary-linux-run: ternary-linux-build ternary-linux-userspace
	$(SCRIPTS)/ternary-linux-run.sh

ternary-linux-isa:
	$(SCRIPTS)/ternary-linux-isa.sh

clean:
	rm -rf "$(WORK)build"
	-git -C "$(TERNARY_REPO)" worktree prune >/dev/null 2>&1

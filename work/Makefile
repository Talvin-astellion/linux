# SPDX-License-Identifier: GPL-2.0-only
WORK := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO := $(abspath $(WORK)/..)
SCRIPTS := $(WORK)pipelines/scripts

.PHONY: help deps build machine checkpatch sparse kunit security-kunit smoke smoke-kasan lint metrics ci-check clean

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

clean:
	rm -rf "$(WORK)build"

WORK := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO := $(abspath $(WORK)/..)
SCRIPTS := $(WORK)pipelines/scripts

.PHONY: help deps build checkpatch sparse kunit ci-install clean

help:
	@echo "Targets:"
	@echo "  deps        install build dependencies"
	@echo "  build       allmodconfig build"
	@echo "  checkpatch  run checkpatch against BASE (default origin/master)"
	@echo "  sparse      run sparse on defconfig"
	@echo "  kunit       run KUnit (UML)"
	@echo "  ci-install  install GitHub Actions workflows"
	@echo "  clean       remove $(WORK)build"

deps:
	$(SCRIPTS)/deps.sh

build:
	$(SCRIPTS)/build.sh

checkpatch:
	$(SCRIPTS)/checkpatch.sh

sparse:
	$(SCRIPTS)/sparse.sh

kunit:
	$(SCRIPTS)/kunit.sh

ci-install:
	$(WORK)pipelines/install.sh

clean:
	rm -rf "$(WORK)build"

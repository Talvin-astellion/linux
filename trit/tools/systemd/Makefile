SD_TOOLS := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
SCRIPTS := $(SD_TOOLS)scripts

.PHONY: help check verify manifest verify-manifest verify-manifest-switch-root dtb build initrd switch-root-initrd run run-switch-root all clean

help:
	@echo "Targets:"
	@echo "  check   report systemd-initrd prerequisites (non-blocking)"
	@echo "  verify  fail if prerequisites are missing"
	@echo "  manifest         write the current artifact manifest"
	@echo "  verify-manifest  compare artifacts against the last successful initrd run"
	@echo "  verify-manifest-switch-root  verify the last successful switch-root run"
	@echo "  dtb     build the Sv32 DTB used by the emulator"
	@echo "  build   cross-build systemd from ~/System/systemd"
	@echo "  initrd  assemble systemd initramfs with PID 1 smoke unit"
	@echo "  switch-root-initrd  assemble initramfs with a /newroot OS tree"
	@echo "  run     boot the initramfs under emu/tern-linux"
	@echo "  run-switch-root     boot and verify switch-root + new-root PID 1"
	@echo "  all     build + initrd + run"

check:
	bash "$(SCRIPTS)/systemd-check.sh" report

verify:
	bash "$(SCRIPTS)/systemd-check.sh" require

manifest:
	bash "$(SCRIPTS)/systemd-manifest.sh"

verify-manifest:
	bash "$(SCRIPTS)/systemd-check.sh" manifest

verify-manifest-switch-root:
	RUN_MODE=switch-root bash "$(SCRIPTS)/systemd-check.sh" manifest

dtb:
	bash "$(SCRIPTS)/systemd-dtb.sh"

build: dtb
	bash "$(SCRIPTS)/systemd-build.sh"

initrd:
	bash "$(SCRIPTS)/systemd-initrd.sh"

switch-root-initrd: initrd
	bash "$(SCRIPTS)/systemd-switch-root.sh"

run:
	bash "$(SCRIPTS)/systemd-run.sh"

run-switch-root: switch-root-initrd
	bash "$(SCRIPTS)/systemd-run-switch-root.sh"

all: build initrd run

clean:
	rm -rf "$(SD_TOOLS)build"

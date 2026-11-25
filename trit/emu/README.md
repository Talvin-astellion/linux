# Ternary CPU Emulator

A small emulator for the balanced-ternary CPU.  The core is dependency-free C;
the analysis tools are optional Python 3 scripts.

## Files

| File | Description |
| --- | --- |
| `cpu.c`, `cpu.h` | CPU state, instruction execution, register helpers |
| `mem.c`, `mem.h` | RAM and memory-mapped I/O bus |
| `ternary.c`, `ternary.h` | Balanced-ternary arithmetic and parsing |
| `disassem.c`, `disassem.h` | Instruction decoding and disassembly |
| `main.c` | Command-line monitor |
| `tml.c`, `tml.h` | Ternary Moral Logic (TML) runtime and state machine |
| `tml_policy.c`, `tml_policy.h` | Policy defaults and `key=value` policy loader |
| `tml_log.c`, `tml_log.h` | Append-only JSONL Always Memory with hash chaining |
| `sha256.c`, `sha256.h` | SHA-256 used by Always Memory |
| `trace.c`, `trace.h` | Instruction / taint / memory JSONL tracing |
| `taint.c`, `taint.h` | Register and memory taint propagation |
| `symbolic.c`, `symbolic.h` | Lightweight collection of conditional constraints |
| `rv32.c` | RV32IMAFDC guest interpreter (`tern-linux`) with Sv32 MMU |
| `tests/` | Unit tests run by `make test` / `make test-rv32` |

## Build and test

```sh
make -C emu                 # builds emu/tern-cpu-emu
make -C emu test            # core, TML and trace/taint/symbolic tests
make -C emu sanitize        # same tests with ASan and UBSan
make -C emu test-tools      # Python verifier and cross-process log recovery tests
make -C emu test-rv32       # host-side Sv32 tests and deterministic differential fuzz
make -C emu test-rv32-guest # bare-metal Sv32 guest test (needs RV32 cross gcc)
make -C emu bench           # compare cached and uncached instruction decoding
make -C emu bench-rv32-fpu  # compare RV32 RNE and quadmath RMM FP paths
```

`make -C emu all` additionally builds Doxygen documentation and therefore
requires `doxygen`; plain `make` does not.

## Instruction decoding and performance

`parseInstruction()` uses a bounded, direct-mapped cache of 256 decoded words,
shared by CPU execution, disassembly, tracing, taint and symbolic analysis.
Entries are filled on demand (cache-aside); a collision replaces one entry, and
the complete instruction word is checked before returning a cached copy.  Memory
usage stays fixed, with no allocation in the execution loop.

The key is the instruction's contents rather than its address.  Instruction
fetches still read the memory bus on every step, so guest stores, monitor writes,
RAM aliases, program reloads and memory resets take effect immediately.  Cached
fields contain no CPU or memory state and need no reset-time invalidation.

This follows the [System Design Primer's cache-aside approach](https://github.com/donnemartin/system-design-primer#cache-aside)
and its [benchmark/profile/optimize workflow](https://github.com/donnemartin/system-design-primer/blob/master/solutions/system_design/scaling_aws/README.md).
The benchmark runs the same deterministic programs with and without the cache,
checks the resulting CPU state, and reports elapsed monotonic time and millions
of instructions per second:

```sh
make -C emu bench BENCH_CYCLES=3000000
```

Workloads cover a NOP loop, an arithmetic loop, arithmetic with taint tracking,
and 512 distinct NOP words that exceed cache capacity.  Repeat runs to compare
timings on your machine; this is a synthetic benchmark, not a timing-based test.
Guest timings can be bimodal due to code layout and host scheduling; pin runs to
one idle core, interleave binaries and compare medians rather than single runs.
To profile either version directly, use `emu/bench-cpu [N]` or
`emu/bench-cpu-uncached [N]`.  The latter is compiled with
`-DTERNARY_DECODE_CACHE=0`, which can also disable caching in a normal build.

The RV32 floating-point benchmark compares round-to-nearest with the
`libquadmath` RMM path for single- and double-precision add/sub loops:

```sh
make -C emu bench-rv32-fpu BENCH_FPU_OPS=5000000
```

Each workload starts from the same register state, runs an even number of
operations, and checks the final FP register value before reporting elapsed time
and millions of operations per second.  It is a synthetic timing report, not a
timing-based test; repeat runs and compare medians on an idle machine.

RAM address decoding uses a 64-byte lookup table for three-trit chunks instead
of converting a full 32-trit number.  Memory and taint tracking share this
decoder, including its low-eleven-trit address wrapping and bounds checks.
The core tests exhaust all 22-bit address encodings against the general
conversion, including undefined trits and high-bit aliases.

Balanced-ternary addition and carry addition stop scanning once the carry is
clear and the remaining trits of the second operand are zero, so PC, clock and
small-address increments do not touch all 32 trits.  Differential tests compare
both fast paths against reference adders over random operands, undefined-trit
encodings, all carry inputs and long carry chains.

Register reads skip the tritwise modifier gate when it is the identity
(`OPMOD_NOP`) and the value contains no undefined trits; undefined trits would
otherwise be mapped to zero.  Tests cover both trit kinds and confirm that the
shift modifier is applied before the identity check.

Arithmetic flags derive parity by collapsing every trit to one nonzero bit and
using the compiler's parity builtin instead of walking the 27 trits.  A
differential test checks the resulting P flag over random defined-trit words.

Trace taint events are produced only while a trace file is open; otherwise the
register-mask scan is skipped even when taint tracking itself is enabled.

Taint propagation records only the registers pending in the current instruction
instead of clearing and scanning all 64 registers.  A phase-2 test checks RTL
writes to two registers and confirms the next instruction does not replay them.

Immediate expansion converts only the three selector trits instead of the full
word.  It stays out of line so the decode-cache hit path keeps its compact
shape; a differential test checks expanded 11- and 9-trit fields.

For assembled guest workloads, run `make -C asm bench BENCH_RUNS=5`.  This builds
`emu/bench-guest` and measures only CPU execution, excluding image loading and
reset.  Each run starts with a fresh image, must reach HALT within `BENCH_LIMIT`
(default 1000000 instructions), and checks `P01` and a repeatable instruction
count.  The workloads fill and sum negative RAM through a positive alias, and
exercise modular exponentiation with library calls, branches and stack traffic.
See [the assembler benchmarks](../asm/README.md#guest-benchmarks) for details.

## Monitor

```sh
./emu/tern-cpu-emu [options] [program.tas]
```

| Option | Description |
| --- | --- |
| `--tml` | Enable the TML runtime |
| `--tml-log FILE` | Always Memory JSONL path (default `tml_memory.jsonl`) |
| `--tml-policy FILE` | Load policy thresholds from a file |
| `--trace FILE` | Write an instruction trace JSONL |
| `--taint` | Enable taint tracking |
| `--symbolic FILE` | Write conditional constraints as JSONL |

`HALT` stops the CPU until `c` resets it.  Invalid opcodes and malformed
register fields latch a trap and stop the CPU instead of executing as NOPs.
`REG_CLOCK` counts executed instructions; the flags register tracks sign,
carry, overflow and trit parity.

Interactive commands:

| Command | Action |
| --- | --- |
| `<Enter>` | Step one instruction |
| `g [N]` | Run up to `N` instructions (default 100000), stopping on FREEZE |
| `s` | Print CPU state |
| `r <ADDR>` / `w <ADDR> <VAL>` | Peek / poke a tryte |
| `c` / `m` | Reset CPU / memory |
| `f <FILE>` | Load a file into memory |
| `t` | Print TML status |
| `l` | Print the tail of the Always Memory log |
| `o +|-|0` | Human oversight override: PROCEED / REFUSE / SACRED_ZERO |
| `u` | Recover from FREEZE with the built-in unfreeze token |
| `x` | Print taint state (`--taint`) |
| `y` | Print collected symbolic constraints |
| `q` | Quit |

Addresses and values are entered in balanced ternary (`-`, `0`, `1`).

## TML runtime

The runtime implements the fail-closed TML state machine:

```
BOOT -> ACTIVE -> SACRED_PAUSE -> FREEZE -> RECOVERY
```

* `PROCEED` (`+1`), `SACRED_ZERO` (`0`), `REFUSE` (`-1`).
* `SACRED_PAUSE` blocks external actuation while evidence is collected.
* `FREEZE` is latched; only the unfreeze token can enter `RECOVERY`.
* A Type-0 anomaly (actuation without a logged decision) triggers `FREEZE`.
* Every boot, decision, override, freeze and recovery is appended to Always
  Memory as JSONL with `prev_hash` / `hash` SHA-256 chaining.

Opening an existing Always Memory file verifies every record's sequence number,
previous hash and SHA-256 digest before enabling appends.  The next sequence
number, head hash and MMIO head value are restored, so restarts continue one
chain.  Verification is a streaming scan with bounded memory; startup time is
linear in the log size.  It accepts the emulator's emitted JSONL format,
including escaped strings and its existing canonical hash representation.

A malformed or incomplete record, sequence discontinuity or hash mismatch makes
initialization fail without changing the file.  This includes older files
containing multiple chains restarted at sequence zero.  A failed write or flush
leaves the in-memory head unchanged and disables further appends on that handle.
The analysis tools
also require continuous sequence numbers instead of accepting a new sequence
zero as a chain reset.

Malformed JSONL and incomplete final lines are errors in the Python analysis
tools as well; they are not skipped when producing a chain status or report.

### MMIO

The TML registers are memory-mapped at negative ternary addresses:

| Address | Name | Access | Meaning |
| --- | --- | --- | --- |
| `-26` | `ADDR_UART_STATUS` | read | +1 input ready, 0 empty, -1 EOF |
| `-17` | `ADDR_TML_STATUS` | read | FSM state: 0 BOOT, 1 ACTIVE, 2 SACRED_PAUSE, 3 FREEZE, 4 RECOVERY |
| `-18` | `ADDR_TML_ACTION` | read/write | Writing submits an action id and evaluates it |
| `-19` | `ADDR_TML_ARG0` | read/write | Action argument 0 |
| `-20` | `ADDR_TML_ARG1` | read/write | Action argument 1 |
| `-21` | `ADDR_TML_ARG2` | read/write | Action argument 2 |
| `-22` | `ADDR_TML_VERDICT` | read/write | Last verdict or human override |
| `-23` | `ADDR_TML_RISK` | read/write | Risk score 0..100 |
| `-24` | `ADDR_TML_LOG_HEAD` | read | Tryte derived from the current log head |
| `-25` | `ADDR_TML_UNFREEZE` | write | Unfreeze token (4242) |

The symbolic constants are defined in `mem.h`.  Standard input supplies UART
input to programs, and `ADDR_UART_STATUS` reports when a byte is ready, empty or
EOF.  A read from `ADDR_UART` returns 0 when no character is ready, so programs
should poll `ADDR_UART_STATUS` first.  The monitor uses the same input stream,
so serialized input should follow a run command such as `g\n`.

### Policy file

`--tml-policy` accepts `key=value` lines; `#` starts a comment.

```ini
system_id=ternary-emu
log=tml_memory.jsonl
tau_pause=60
tau_refuse=85
require_decision=1
taint_penalty=25
taint_freeze=0
```

`tau_pause` / `tau_refuse` are inclusive risk thresholds.  When taint is
enabled, a tainted action adds `taint_penalty` to its risk score; with
`taint_freeze=1` a tainted action at or above `tau_refuse` enters `FREEZE`.

## RV32 guest MMU and systemd initrd

`tern-linux` implements Sv32 address translation for RV32 guests.  Writing
`satp` with mode `Sv32` enables two-level page tables with 4 KiB and 4 MiB
pages; `Bare` keeps the previous physical-address behaviour.  Access rights,
`mstatus.SUM`/`MXR`, accessed/dirty bits and instruction/load/store page faults
(causes 12/13/15) are emulated.  A 128-set, 2-way TLB caches successful
translations; changes to the `satp` root/mode flush it, while ASID-only changes
reuse ASID-tagged entries.  `sfence.vma` honors `rs1`/`rs2` for address/ASID-scoped
invalidation, and permission checks still use the current `priv`/`mstatus` and
page-table PMP permissions on every hit.  Supervisor fetches from
user pages always fault, even when `SUM` is set, and 4 MiB leaves with a
nonzero low PPN or a `W`-without-`R` mapping are rejected.  Multi-byte loads and
stores stop at the first faulting byte, so a page-crossing access cannot hide
an earlier translation fault.  `lr.w`, `sc.w` and AMOs require natural 4-byte
alignment; misaligned atomics raise a load-address-misaligned trap, reserved
`lr.w`/`sc.w` ordering encodings are illegal, and any successful store or AMO
clears the local reservation.  `fence` and `fence.i` are no-ops in this
single-core model.  RV32C instructions are decompressed before execution, with
2-byte fetches and page-boundary handling; RV64-only encodings are illegal.
The F/D extensions provide 32 floating-point registers, `fcsr`/`fflags`/`frm`,
single and double arithmetic, conversions, comparisons, classification, fused
multiply-add and IEEE rounding/exception flags; compressed FP loads/stores are
also decoded.  CSR access is privilege checked: user mode may read
`cycle`/`time`/`instret` and access the FP CSRs, while supervisor/machine CSRs,
unknown CSRs and writes to read-only CSRs raise illegal-instruction traps.
Machine-mode traps use `mtvec`/`mepc`/`mcause`/`mtval`, `mret` restores privilege
from `MSTATUS.MPP`, `medeleg`/`mideleg` route traps to S-mode, and the PMP CSRs
restrict physical-memory and fetch permissions.  PMP is default-deny for S/U;
boot installs a grant-all NAPOT entry that guests may override.  A minimal
CLINT exposes `mtime`/`mtimecmp` at `0x0200bff8`/`0x02004000`; both CLINT
writes and the SBI timer extension program the same timer deadline.  Wall-clock
tick conversion avoids 64-bit overflow.  Timer interrupts are level-triggered
and stay pending until `mtimecmp` is reprogrammed.
Deterministic differential
fuzzing compares `pmp_check` and Sv32 translation (with and without PMP) against
independent reference models; `make -C emu test-rv32` runs the fuzz cases.
M-mode external/timer/software interrupts (causes 11/7/3) are supported;
interrupts delegated by `mideleg` are masked at M-mode.  Supervisor interrupts
honor direct/vectored `stvec`, prioritize external, software, then timer sources,
and wake `wfi` only when an enabled interrupt is pending.  Use `--debug-mmu` to
log translation failures.
`--stats` prints retired instructions, Sv32 walks, TLB hits/misses, page faults,
wall time and guest time at exit.

The systemd-as-PID-1 pipeline builds a systemd initramfs for an MMU-enabled
RV32 guest and boots it with `--initrd`.  See
[`tools/systemd/README.md`](../tools/systemd/README.md) for prerequisites,
kernel config and the complete workflow.

`tern-linux` checks that the kernel, DTB, initramfs and any `--load` files fit
in RAM without overlapping before loading them. Use `--dtb-addr` or
`--initrd-addr` to select a different layout for larger images.

## Analysis tools

```sh
python3 tools/tml_analyze.py tml_memory.jsonl trace.jsonl
python3 tools/tml_report.py tml_memory.jsonl -o report.md
```

`tml_analyze.py` summarizes trace events, opcodes, verdicts, risk and verifies
the Always Memory hash chain.  `tml_report.py` emits a Markdown report with a
timeline, freeze evidence and chain status.  The trace records instruction
fields and taint changes; `--symbolic` adds conditional constraints from `ADS`
and `ADSI` instructions.

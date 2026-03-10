# Quadrilatero Matrix Extension for gem5 RISC-V

## Scope

This document consolidates the quadrilatero-related changes made to gem5 for
RISC-V. It covers:

- ISA plumbing and decode support
- Matrix register model changes
- O3CPU execution and memory modeling changes
- Functional-unit configurability impact
- Test workloads and regression coverage
- Current limitations and known approximation points

This summary is intended to be the single reference for the current state of
the implementation.

## Source Design and Plan

The implementation was originally driven by these two documents:

- `docs/plans/2026-03-09-quadrilatero-matrix-gem5-design.md`
- `docs/plans/2026-03-09-quadrilatero-matrix-gem5-implementation-plan.md`

This document summarizes the final implemented state after the follow-up fixes
and test additions.

## High-Level Outcome

The current branch adds a functional quadrilatero-style matrix extension to
gem5's RISC-V ISA model and integrates it with gem5's O3CPU pipeline and
`fuPool`-based execution model.

Implemented instruction families:

- `mld_w`
- `mst_b`
- `mst_h`
- `mst_w`
- `mzero`
- `mmaqa_b`
- `mmada_h`
- `mmasa_w`
- `fmmacc_b`
- `fmmacc_h`
- `fmmacc_s`

The implementation is function-first. The memory path for matrix load/store is
modeled with row-level micro-ops so it can execute correctly on O3CPU rather
than using a single atomic execute-time memory access.

## Commit Summary

Primary implementation commits:

- `2e2649a634` `docs: add quadrilatero matrix extension design for riscv`
- `6cec5dc4ae` `docs: add quadrilatero matrix implementation plan`
- `853091eee7` `arch-riscv: add quadrilatero matrix ISA plumbing`
- `27c85a0e0c` `arch-riscv: add matrix register ISA scaffolding`
- `a2eb7e9dd4` `arch-riscv: decode quadrilatero matrix instructions`
- `af8c82f2a5` `arch-riscv: model quadrilatero matrix memory as microops`
- `416f715a65` `arch-riscv: fix quadrilatero matrix memory dynamic state`

Test and workload commits:

- `afd875ff0d` `tests: add xheep-style quadrilatero matmul workload`
- `a9c750c1fa` `tests: add quadrilatero SE regression coverage`
- `62749195d8` `tests: add quadrilatero store packing coverage`
- `e9e186fc19` `tests: add quadrilatero int8 and int16 workloads`
- `b4129d8411` `tests: add quadrilatero fmmacc-s workload`
- `b14f845a8c` `tests: add quadrilatero fmmacc-h workload`
- `1f6492d262` `tests: add quadrilatero fmmacc-b workload`

Cleanup commits removing generated binaries from the repository:

- `8ddafdd6d0` `tests: drop generated quadrilatero store binary`
- `cf7a9ac555` `tests: drop generated quadrilatero int binaries`

## ISA and Decode Integration

### Decode entry point

Quadrilatero instructions are decoded through the RISC-V custom opcode path:

- `src/arch/riscv/isa/decoder.isa`

The extension is attached under the quadrilatero enable gate and routed through
the `CUSTOM1`-style decode structure already aligned with the quadrilatero
decoder behavior.

### Decode coverage

The implemented decode set includes:

- `mld_w`
- `mst_b`
- `mst_h`
- `mst_w`
- `mzero`
- `mmaqa_b`
- `mmada_h`
- `mmasa_w`
- `fmmacc_b`
- `fmmacc_h`
- `fmmacc_s`

The implementation intentionally ignores commented-out or unsupported entries
from the RTL decoder source.

### Semantic alignment

The arithmetic semantics were aligned with the quadrilatero/x-heep usage model:

- `Ms2` is treated as the data tile
- `Ms1` is treated as the weight tile
- `Md` is the accumulator/output tile

For example, the x-heep-style sequence:

- `mld.w m1, B`
- `mld.w m0, A`
- `mmasa.w m2, m1, m0`

produces the expected matrix result under the implemented software reference.

## Matrix Register Model

### Register class

The matrix register model is defined in:

- `src/arch/riscv/regs/mat.hh`

Key properties:

- `NumMatRegs = 8`
- Matrix registers are exposed as `m0` through `m7`
- Storage uses `MatRegContainer = gem5::MatStore<16, 4>`

### Internal representation

The internal model stores a 4x4 matrix tile as:

- 16 logical 32-bit words
- 4 rows
- 16 bytes per row

The same raw storage is reinterpreted as:

- `int8_t` for `mmaqa_b` / `fmmacc_b`
- `int16_t` / fp16 bit patterns for `mmada_h` / `fmmacc_h`
- `int32_t` / fp32 bit patterns for `mmasa_w` / `fmmacc_s`

### Row packing helpers

`mat.hh` also contains the helper functions used by the memory micro-ops:

- row byte serialization
- row word deserialization
- per-macro load state support

## Instruction Format and Execution Templates

Matrix instruction format support lives in:

- `src/arch/riscv/isa/formats/matrix.isa`

This file now provides:

- regular matrix op templates
- matrix macro instruction templates
- matrix load row micro-op templates
- matrix store row micro-op templates

### Implemented op behavior

Arithmetic instructions:

- `mzero` clears the destination matrix register
- `mmaqa_b` performs 16-element signed byte dot products per output element
- `mmada_h` performs 8-element signed halfword dot products per output element
- `mmasa_w` performs 4-element signed word dot products per output element
- `fmmacc_s` performs 4-element fp32 dot products per output element
- `fmmacc_h` converts fp16 bit patterns to fp32 and accumulates in fp32
- `fmmacc_b` currently treats 8-bit lanes as integer values converted to float
  before accumulation

Writeback is explicit in the matrix op template through
`xc->setRegOperand(this, 0, &Md)`, which was necessary to make `mzero` and the
MAC instructions commit their results correctly.

## Matrix Memory Modeling

### Why load/store had to be reworked

The first functional implementation used direct atomic memory accesses inside
instruction execution. That was sufficient for basic functional correctness on
simple CPU models, but it was not compatible with O3CPU's normal memory path.

To fix this, matrix memory instructions were remodeled as macro instructions
that expand into row-level micro-ops.

### Current memory model

Implemented in:

- `src/arch/riscv/isa/formats/matrix.isa`
- `src/arch/riscv/regs/mat.hh`

Behavior:

- `mld_w` expands into 4 row-load micro-ops
- `mst_b`, `mst_h`, `mst_w` each expand into 4 row-store micro-ops

Row granularity:

- one row = 16 bytes
- one macro instruction = 4 row micro-ops

### Architectural visibility

`mld_w` uses atomic final visibility semantics:

- the 4 row loads fill a temporary tile buffer
- the matrix destination register is only committed after all rows complete

This matches the chosen "commit only at the end" design.

### Store packing behavior

Store behavior is defined by output view, not by changing the underlying matrix
register layout:

- `mst_w`: 4 x 32-bit words per row
- `mst_h`: each 32-bit word split into low 16 bits then high 16 bits
- `mst_b`: each 32-bit word split into 4 little-endian bytes

This behavior is now covered by dedicated regression workloads.

## O3CPU Dynamic State Fixes

### Problem

The first row-micro-op implementation incorrectly stored execution-time matrix
state on static instruction objects. That breaks on O3CPU because multiple
dynamic instances of the same static instruction can overlap in flight.

### Fix

The final O3-safe implementation stores matrix macro state per dynamic
execution.

Relevant files:

- `src/cpu/o3/dyn_inst.hh`
- `src/cpu/o3/dyn_inst.cc`
- `src/cpu/o3/fetch.hh`
- `src/cpu/o3/fetch.cc`
- `src/cpu/o3/lsq.hh`
- `src/cpu/o3/lsq.cc`
- `src/arch/riscv/regs/mat.hh`

### Dynamic state model

O3 now uses macro-level dynamic state:

- `o3::MacroDynState`
- attached to `DynInst`
- shared across the micro-ops of a single macro instruction instance

`mld_w` uses this to hold:

- temporary load tile contents
- row-valid state

For non-O3 execution contexts, a fallback state object is still used, but it is
now shared correctly across the 4 micro-ops of the same macro instruction.

### Store payload lifetime

O3 store correctness also required request-owned data payloads:

- store row bytes are copied into the LSQ request
- packet build and retries use the request-owned buffer
- this avoids reading invalid stack or overwritten shared storage

Without this fix, O3 matrix matmul produced corrupted results.

## O3 Register Pressure Adjustment

The default O3 physical matrix register count was increased in:

- `src/cpu/o3/BaseO3CPU.py`

This was necessary to run the new workloads without artificial early failures
from matrix register pressure.

## Functional Unit and OpClass Implications

The implementation uses dedicated matrix-related op classes so the instructions
remain configurable through gem5's O3 `fuPool`.

This enables runtime FU configuration for matrix classes while preserving the
quadrilatero instruction family split.

The design goal here was:

- decode correctness
- functional execution
- O3 compatibility
- `fuPool` configurability

not exact quadrilatero hardware timing fidelity.

## Test Workloads

### Base workloads

Added under `tests/test-progs/`:

- `quadrilatero-smoke`
- `quadrilatero-matmul`
- `quadrilatero-xheep-matmul`

Purpose:

- `quadrilatero-smoke`: minimal `mld_w + mzero + mmasa_w + mst_w`
- `quadrilatero-matmul`: larger integer matrix multiply
- `quadrilatero-xheep-matmul`: x-heep-style kernel structure in gem5 SE form

### Store packing workload

- `quadrilatero-store-pack`

Purpose:

- validates `mst_h`
- validates `mst_b`
- checks little-endian row packing semantics

### Integer MAC workloads

- `quadrilatero-mmaqa-b`
- `quadrilatero-mmada-h`

Purpose:

- validates byte-view integer accumulation
- validates halfword-view integer accumulation

### Floating MAC workloads

- `quadrilatero-fmmacc-s`
- `quadrilatero-fmmacc-h`
- `quadrilatero-fmmacc-b`

Purpose:

- `fmmacc_s`: fp32 path
- `fmmacc_h`: fp16-to-fp32 accumulation path
- `fmmacc_b`: current gem5 approximate behavior, not true FP8 validation

## Regression Harness

Formal SE regression coverage was added in:

- `tests/gem5/se_mode/quadrilatero/configs/local_binary_run.py`
- `tests/gem5/se_mode/quadrilatero/test_quadrilatero_se.py`

Features:

- auto-build local test-progs with `MakeFixture`
- run on local ELF paths
- cover both `atomic` and `o3`
- validate normal simulation exit

The current regression list includes all quadrilatero workloads added in this
branch.

## Verification Summary

The following categories have been explicitly verified during implementation:

- `mld_w/mst_w` on `AtomicSimpleCPU`
- `mld_w/mst_w` on `O3CPU`
- x-heep-style `mmasa_w` matmul on `AtomicSimpleCPU`
- x-heep-style `mmasa_w` matmul on `O3CPU`
- `mst_h/mst_b` packing on `AtomicSimpleCPU`
- `mst_h/mst_b` packing on `O3CPU`
- `mmaqa_b` on `AtomicSimpleCPU`
- `mmaqa_b` on `O3CPU`
- `mmada_h` on `AtomicSimpleCPU`
- `mmada_h` on `O3CPU`
- `fmmacc_s` on `AtomicSimpleCPU`
- `fmmacc_s` on `O3CPU`
- `fmmacc_h` on `AtomicSimpleCPU`
- `fmmacc_h` on `O3CPU`
- `fmmacc_b` on `AtomicSimpleCPU`
- `fmmacc_b` on `O3CPU`

## Known Limitations

### `fmmacc_b` is approximate

`fmmacc_b` is not modeled as true FP8. The current implementation interprets
8-bit lanes as signed integer values converted to float before accumulation.

The workload added for `fmmacc_b` intentionally validates this implemented
behavior rather than claiming FP8 conformance.

### Timing model is still approximate

The implementation is function-first and O3-correct, but it is not a detailed
quadrilatero hardware timing model. In particular:

- row micro-op memory behavior is modeled
- true hardware execution unit timing, contention, and throughput are not yet
  matched in detail

### Only implemented decode subset is supported

Commented-out or unsupported quadrilatero instructions from the RTL decoder are
still intentionally excluded.

## Current Code Map

Architecture and ISA:

- `src/arch/riscv/isa/decoder.isa`
- `src/arch/riscv/isa/formats/matrix.isa`
- `src/arch/riscv/regs/mat.hh`

O3 integration:

- `src/cpu/o3/BaseO3CPU.py`
- `src/cpu/o3/dyn_inst.hh`
- `src/cpu/o3/dyn_inst.cc`
- `src/cpu/o3/fetch.hh`
- `src/cpu/o3/fetch.cc`
- `src/cpu/o3/lsq.hh`
- `src/cpu/o3/lsq.cc`

Regression entry:

- `tests/gem5/se_mode/quadrilatero/configs/local_binary_run.py`
- `tests/gem5/se_mode/quadrilatero/test_quadrilatero_se.py`

Workloads:

- `tests/test-progs/quadrilatero-smoke/src/`
- `tests/test-progs/quadrilatero-matmul/src/`
- `tests/test-progs/quadrilatero-xheep-matmul/src/`
- `tests/test-progs/quadrilatero-store-pack/src/`
- `tests/test-progs/quadrilatero-mmaqa-b/src/`
- `tests/test-progs/quadrilatero-mmada-h/src/`
- `tests/test-progs/quadrilatero-fmmacc-s/src/`
- `tests/test-progs/quadrilatero-fmmacc-h/src/`
- `tests/test-progs/quadrilatero-fmmacc-b/src/`

## Recommended Next Steps

If this branch continues, the most natural next tasks are:

1. Add a review pass over the full quadrilatero branch to look for ISA,
   decode, or O3 integration regressions.
2. Add performance-oriented experiments to observe matrix OpClass and `fuPool`
   configuration effects on cycle counts.
3. If higher fidelity is needed, replace the current `fmmacc_b` approximation
   with a more accurate low-precision floating-point model.

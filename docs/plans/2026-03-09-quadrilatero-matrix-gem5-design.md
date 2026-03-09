# Quadrilatero Matrix Extension for gem5 RISC-V - Design

Date: 2026-03-09
Status: Approved

## 1. Scope and Decisions

This design adds Quadrilatero-based matrix instructions to gem5 RISC-V and exposes fine-grained OpClass controls for O3CPU `fuPool` runtime configuration.

Confirmed decisions:
- Priority: functionality first (not cycle-accurate hardware timing in v1).
- Coverage: all instructions that are decodable in `rtl/quadrilatero/rtl/quadrilatero_decoder.sv`.
- Source of truth: `rtl/quadrilatero/rtl/quadrilatero_decoder.sv`.
- Ignore commented or unimplemented instruction families in RTL (e.g., `MCFG*`).
- OpClass granularity: add new fine-grained matrix OpClasses.
- Feature gate: add enable switch and default it to enabled.

## 2. Architecture

The implementation is split into five layers:

1. Decode layer (RISC-V ISA):
- Add `CUSTOM1` (`opcode = 0b0101011`) decode branch in RISC-V ISA decode.
- Decode sub-opcodes using field patterns aligned with `quadrilatero_decoder.sv`.
- Unsupported/custom sub-encodings fall back to illegal instruction.

2. StaticInst and semantic layer:
- Add instruction definitions and execution semantics for matrix arithmetic, matrix load/store, and zero.
- Use correctness-first semantics with fixed baseline latency classes.

3. Register model layer:
- Set RISC-V matrix register class count to 8 (`m0-m7`) instead of 0.
- Keep data container layout compatible with 4x4x32-bit tile model, with 8/16-bit packed interpretation for relevant arithmetic ops.

4. OpClass/FU mapping layer:
- Add fine-grained matrix OpClasses (see section 4).
- Bind each decoded instruction directly to one new OpClass.

5. Feature gate layer:
- Add ISA parameter `enable_quadrilatero_matrix`, default `True`.
- When disabled, all quadrilatero matrix instructions are treated as illegal instructions.

## 3. Decode and Data Flow

Execution flow:
- `fetch -> decode -> StaticInst -> execute -> writeback`

Decode constraints:
- Major opcode: `0101011`
- `func3`: `000`
- Then decode based on high-bit patterns matching RTL decoder cases.

Register field mapping:
- Arithmetic ops: `ms2=instr[23:21]`, `ms1=instr[20:18]`, `md=instr[17:15]`
- Load/store ops: matrix reg field from `instr[9:7]` (destination for load, source for store)

Instruction family mapping:
- Floating MAC family (`FMMACC_*`) -> floating matrix MAC OpClass
- Integer MAC family (`MMAQA_B`, `MMADA_H`, `MMASA_W`) -> integer matrix MAC OpClass
- `MZERO` -> matrix zero OpClass
- `MLD_*` -> matrix load OpClass
- `MST_*` -> matrix store OpClass

## 4. OpClass Plan (Fine-Grained)

Add the following OpClasses:
- `MatrixLoad`
- `MatrixStore`
- `MatrixMacInt`
- `MatrixMacFp`
- `MatrixZero`

Rationale:
- Allows independent O3 `fuPool` runtime tuning (count, latency, pipelining) by operation family.
- Meets requirement for configurable execution resources beyond coarse `Matrix/MatrixMov/MatrixOP` buckets.

## 5. Semantic Behavior (v1)

Arithmetic:
- Correct matrix accumulate behavior by data type family according to RTL-decoded operation kind.
- Integer and floating paths are separated by instruction family.

Zero:
- `MZERO` clears destination matrix register tile.

Load/store:
- Base from `rs1`, stride from `rs2`, tile moved row by row.
- v1 keeps normal gem5 memory ordering behavior; no special non-coherent side effects are modeled.

## 6. Error Handling and Compatibility

Illegal instruction handling:
- Any non-matching CUSTOM1 sub-encoding triggers illegal instruction.
- With feature gate off, all quadrilatero matrix instructions trigger illegal instruction.

Compatibility boundaries:
- Default-enable behavior changes interpretation of this CUSTOM1 region in RISC-V.
- Must document that this encoding space is consumed by quadrilatero extension in this build.

## 7. Testing and Acceptance

Tests:
1. Decode tests:
- One positive decode sample per supported instruction encoding family.
- Negative decode cases for unsupported CUSTOM1 sub-encodings.

2. Semantic tests:
- `MZERO`: destination tile becomes all zero.
- `MLD/MST`: round-trip load/store tile consistency.
- MAC families: deterministic reference-vector checks for integer/floating families.

3. O3 fuPool configurability tests:
- Configure distinct `opLat` and/or FU counts for new matrix OpClasses.
- Verify runtime impact and op scheduling behavior changes.

4. Feature-gate tests:
- Gate on: matrix tests execute successfully.
- Gate off: same instructions trap as illegal.

Acceptance criteria:
- All new tests pass.
- At least one matrix workload executes on O3 and shows behavior change when matrix OpClass FU settings are varied.
- Baseline RISC-V regressions (without matrix instruction usage) remain unaffected.

## 8. Out of Scope (v1)

- Cycle-accurate quadrilatero pipeline/resource modeling.
- Non-coherent CPU/accelerator memory visibility effects.
- Commented/unimplemented RTL instruction families.

# Quadrilatero Matrix Extension Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add Quadrilatero CUSTOM1 matrix instructions to gem5 RISC-V with fine-grained matrix OpClass support configurable through O3CPU `fuPool`, behind a default-enabled ISA gate.

**Architecture:** Extend the RISC-V ISA decoder and instruction formats to recognize Quadrilatero encodings from `rtl/quadrilatero/rtl/quadrilatero_decoder.sv`, wire matrix operands/registers into execution semantics, and map each instruction family to new fine-grained OpClasses. Keep v1 functionally correct first, with fixed-latency approximations and no extra hardware-coherency modeling.

**Tech Stack:** gem5 ISA parser (`*.isa`), C++ generated decoder/StaticInst code, Python SimObject params (`RiscvISA.py`, `FuncUnit.py`, `FuncUnitConfig.py`), gem5 regression tests.

---

### Task 1: Add Feature Gate and Matrix Register Count Baseline

**Files:**
- Modify: `src/arch/riscv/RiscvISA.py`
- Modify: `src/arch/riscv/isa.hh`
- Modify: `src/arch/riscv/isa.cc`
- Test: `build/RISCV/gem5.opt` (build verification)

**Step 1: Write the failing test**

```python
# In src/arch/riscv/RiscvISA.py (temporary assertion during bring-up)
# Accessing this field in C++ should fail before it is defined.
# Expected compile error: no member named enable_quadrilatero_matrix
```

**Step 2: Run test to verify it fails**

Run: `scons build/RISCV/gem5.opt -j$(nproc)`  
Expected: FAIL with missing ISA param/member for quadrilatero gate.

**Step 3: Write minimal implementation**

```python
# RiscvISA.py
enable_quadrilatero_matrix = Param.Bool(
    True, "Enable Quadrilatero matrix custom extension"
)
```

```cpp
// isa.hh
const bool enableQuadrilateroMatrix;

// isa.cc constructor init-list
enableQuadrilateroMatrix(p.enable_quadrilatero_matrix)

// isa.cc reg class setup
RegClass matRegClass(MatRegClass, MatRegClassName, 8, debug::MatRegs);
```

**Step 4: Run test to verify it passes**

Run: `scons build/RISCV/gem5.opt -j$(nproc)`  
Expected: PASS.

**Step 5: Commit**

```bash
git add src/arch/riscv/RiscvISA.py src/arch/riscv/isa.hh src/arch/riscv/isa.cc
git commit -m "arch-riscv: add quadrilatero gate and 8 matrix regs"
```

### Task 2: Add Fine-Grained Matrix OpClasses

**Files:**
- Modify: `src/cpu/FuncUnit.py`
- Modify: `src/cpu/op_class.hh`
- Modify: `src/cpu/o3/FuncUnitConfig.py`
- Modify: `src/cpu/minor/BaseMinorCPU.py`
- Test: `build/RISCV/gem5.opt`

**Step 1: Write the failing test**

```python
# Temporary in FuncUnitConfig.py
OpDesc(opClass="MatrixMacInt")
# Should fail before enum value exists.
```

**Step 2: Run test to verify it fails**

Run: `scons build/RISCV/gem5.opt -j$(nproc)`  
Expected: FAIL with unknown OpClass `MatrixMacInt`.

**Step 3: Write minimal implementation**

```python
# FuncUnit.py OpClass enum additions
"MatrixLoad", "MatrixStore", "MatrixMacInt", "MatrixMacFp", "MatrixZero",
```

```cpp
// op_class.hh aliases
static const OpClass MatrixLoadOp = enums::MatrixLoad;
static const OpClass MatrixStoreOp = enums::MatrixStore;
static const OpClass MatrixMacIntOp = enums::MatrixMacInt;
static const OpClass MatrixMacFpOp = enums::MatrixMacFp;
static const OpClass MatrixZeroOp = enums::MatrixZero;
```

```python
# FuncUnitConfig.py Matrix_Unit.opList
OpDesc(opClass="MatrixLoad"),
OpDesc(opClass="MatrixStore"),
OpDesc(opClass="MatrixMacInt"),
OpDesc(opClass="MatrixMacFp"),
OpDesc(opClass="MatrixZero"),
```

**Step 4: Run test to verify it passes**

Run: `scons build/RISCV/gem5.opt -j$(nproc)`  
Expected: PASS.

**Step 5: Commit**

```bash
git add src/cpu/FuncUnit.py src/cpu/op_class.hh src/cpu/o3/FuncUnitConfig.py src/cpu/minor/BaseMinorCPU.py
git commit -m "cpu: add fine-grained matrix opclasses for quadrilatero"
```

### Task 3: Add RISC-V Matrix Operands and Bitfields

**Files:**
- Modify: `src/arch/riscv/isa/bitfields.isa`
- Modify: `src/arch/riscv/isa/operands.isa`
- Create: `src/arch/riscv/isa/formats/matrix.isa`
- Modify: `src/arch/riscv/isa/formats/formats.isa`
- Test: `build/RISCV/gem5.opt`

**Step 1: Write the failing test**

```cpp
// matrix.isa draft uses these before defining in operands.isa
Md = Ms1;
```

**Step 2: Run test to verify it fails**

Run: `scons build/RISCV/gem5.opt -j$(nproc)`  
Expected: FAIL with unknown operand names (`Md`, `Ms1`, `Ms2`).

**Step 3: Write minimal implementation**

```python
# operands.isa additions
'mc' : 'RiscvISA::MatRegContainer'
'Md':  MatRegOp('mc', 'MD', 'IsMatrix', 1)
'Ms1': MatRegOp('mc', 'MS1', 'IsMatrix', 2)
'Ms2': MatRegOp('mc', 'MS2', 'IsMatrix', 3)
```

```python
# bitfields.isa additions
def bitfield MD  <17:15>;
def bitfield MS1 <20:18>;
def bitfield MS2 <23:21>;
def bitfield MREG <9:7>;
```

```python
# formats/formats.isa include
##include "matrix.isa"
```

**Step 4: Run test to verify it passes**

Run: `scons build/RISCV/gem5.opt -j$(nproc)`  
Expected: PASS with parser generating matrix operand code.

**Step 5: Commit**

```bash
git add src/arch/riscv/isa/bitfields.isa src/arch/riscv/isa/operands.isa src/arch/riscv/isa/formats/matrix.isa src/arch/riscv/isa/formats/formats.isa
git commit -m "arch-riscv: add matrix isa operands and format scaffolding"
```

### Task 4: Implement Decode Table for Quadrilatero CUSTOM1

**Files:**
- Modify: `src/arch/riscv/isa/decoder.isa`
- Modify: `src/arch/riscv/isa/formats/matrix.isa`
- Test: `build/RISCV/gem5.opt`

**Step 1: Write the failing test**

```cpp
// decoder.isa temporary branch
0x0b: return std::make_shared<IllegalInstFault>("qmat TODO", machInst);
```

**Step 2: Run test to verify it fails**

Run: `build/RISCV/gem5.opt --debug-flags=Decode ...` with an instruction stream containing a known matrix opcode.  
Expected: FAIL/trap as illegal instruction.

**Step 3: Write minimal implementation**

```cpp
// decoder.isa pseudostructure
case 0x0b: // CUSTOM1
  if (!isa->enableQuadrilateroMatrix) illegal;
  if (FUNCT3 != 0) illegal;
  // Match high-bit patterns aligned to quadrilatero_decoder.sv
  // route to MZERO/MLD_*/MST_*/MMA*/FMMACC*
```

**Step 4: Run test to verify it passes**

Run: `scons build/RISCV/gem5.opt -j$(nproc)` and replay matrix-encoding smoke test.  
Expected: PASS decode, no illegal trap for supported encodings.

**Step 5: Commit**

```bash
git add src/arch/riscv/isa/decoder.isa src/arch/riscv/isa/formats/matrix.isa
git commit -m "arch-riscv: decode quadrilatero custom1 matrix instructions"
```

### Task 5: Implement Matrix Semantics (Zero + MAC Families)

**Files:**
- Modify: `src/arch/riscv/isa/formats/matrix.isa`
- Test: `tests/gem5/se_mode` style local matrix smoke binary (new binary resource)

**Step 1: Write the failing test**

```c
// matrix_smoke.c snippet
// 1) load known A/B/C tiles
// 2) execute mzero + mmaqa/mmada/mmasa/fmmacc
// 3) compare against software reference; return 1 on mismatch
```

**Step 2: Run test to verify it fails**

Run: `build/RISCV/gem5.opt configs/example/se.py ... -c matrix_smoke`  
Expected: FAIL (wrong result or unimplemented behavior).

**Step 3: Write minimal implementation**

```cpp
// matrix.isa execute snippets (concept)
for (int i = 0; i < 4; ++i)
  for (int j = 0; j < 4; ++j)
    for (int k = 0; k < K; ++k)
      md[i][j] += ms1[i][k] * ms2[j][k];
```

- Map integer MAC to `MatrixMacInt` and floating MAC to `MatrixMacFp`.
- Map `mzero` to `MatrixZero`.

**Step 4: Run test to verify it passes**

Run: same smoke command.  
Expected: PASS (program exits 0).

**Step 5: Commit**

```bash
git add src/arch/riscv/isa/formats/matrix.isa
git commit -m "arch-riscv: implement quadrilatero matrix zero and mac semantics"
```

### Task 6: Implement Matrix Load/Store Semantics

**Files:**
- Modify: `src/arch/riscv/isa/formats/matrix.isa`
- Test: same smoke binary extended with load/store round-trip checks

**Step 1: Write the failing test**

```c
// Add MLD/MST round-trip test in matrix_smoke.c
// write tile -> mld -> mst -> memcmp == 0
```

**Step 2: Run test to verify it fails**

Run: smoke test under gem5.  
Expected: FAIL (memory mismatch).

**Step 3: Write minimal implementation**

```cpp
// matrix load/store concept
Addr base = Rs1;
Addr stride = Rs2;
for (int r = 0; r < 4; ++r)
  for (int c = 0; c < 4; ++c)
    // element access with row stride and 32b lane packing
```

- `MLD_*` -> `MatrixLoad`, `MST_*` -> `MatrixStore`.

**Step 4: Run test to verify it passes**

Run: smoke test under gem5.  
Expected: PASS (round-trip success).

**Step 5: Commit**

```bash
git add src/arch/riscv/isa/formats/matrix.isa
git commit -m "arch-riscv: implement quadrilatero matrix load/store semantics"
```

### Task 7: Add Regression Tests for Gate and O3 fuPool Configurability

**Files:**
- Create: `tests/gem5/riscv_quadrilatero_matrix/configs/run_matrix_smoke.py`
- Create: `tests/gem5/riscv_quadrilatero_matrix/test_quadrilatero_matrix.py`
- Create: `tests/gem5/riscv_quadrilatero_matrix/ref/simout.txt`
- Modify: `tests/gem5/asmtest/tests.py` (optional registration path)

**Step 1: Write the failing test**

```python
# test_quadrilatero_matrix.py
# Case A: gate on -> expect PASS
# Case B: gate off -> expect illegal instruction
# Case C: O3 with two fuPool configs -> expect different ticks
```

**Step 2: Run test to verify it fails**

Run: `tests/main.py run gem5/riscv_quadrilatero_matrix -j1`  
Expected: FAIL before config/script/resource wiring.

**Step 3: Write minimal implementation**

```python
# run_matrix_smoke.py key knobs
core.isa[0].enable_quadrilatero_matrix = True  # or False for negative case
# O3 FU pool profile A/B uses different latencies/counts for MatrixMacInt/Fp
```

**Step 4: Run test to verify it passes**

Run: `tests/main.py run gem5/riscv_quadrilatero_matrix -j1`  
Expected: PASS for positive and negative gate checks; PASS for fuPool sensitivity check.

**Step 5: Commit**

```bash
git add tests/gem5/riscv_quadrilatero_matrix
git commit -m "tests: add riscv quadrilatero matrix regression coverage"
```

### Task 8: Final Verification and Documentation

**Files:**
- Modify: `docs/general_docs/` relevant RISC-V extension page (or create under `docs/` if no existing page)
- Modify: `RELEASE-NOTES.md` (short mention if policy requires)

**Step 1: Write the failing test**

```text
# Verification checklist before docs update:
# - build RISCV succeeds
# - new matrix tests pass
# - no regression in targeted RISCV asm subset
```

**Step 2: Run test to verify it fails (if anything missing)**

Run:
- `scons build/RISCV/gem5.opt -j$(nproc)`
- `tests/main.py run gem5/riscv_quadrilatero_matrix -j1`
- `tests/main.py run gem5/asmtest --length=quick -j1`

Expected: Any missing integration should fail here before finalizing.

**Step 3: Write minimal implementation**

```markdown
Document:
- new ISA gate: enable_quadrilatero_matrix (default True)
- supported instruction families from quadrilatero_decoder.sv
- new OpClasses and fuPool tuning knobs
- known v1 limitations (non-coherency not modeled)
```

**Step 4: Run test to verify it passes**

Run the same three commands again.  
Expected: PASS.

**Step 5: Commit**

```bash
git add docs RELEASE-NOTES.md
git commit -m "docs: describe quadrilatero matrix riscv extension and tuning"
```

## Notes for Execution

- Follow @test-driven-development for each task.
- Use @verification-before-completion before making any success claim.
- Keep commits scoped to one task each.
- Do not implement commented/unimplemented instruction families in `quadrilatero_decoder.sv`.
- `xheep_matrix_spec/README.md` is supplemental only; RTL decoder is authoritative.

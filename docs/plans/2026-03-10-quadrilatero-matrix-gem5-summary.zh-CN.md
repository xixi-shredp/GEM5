# gem5 RISC-V Quadrilatero 矩阵扩展实现总览

## 1. 文档目的

这篇文档用于集中整理当前分支上所有与 quadrilatero 矩阵扩展相关的
gem5 改动，覆盖以下内容：

- RISC-V ISA 和 decode 集成方式
- 矩阵寄存器模型
- O3CPU 下的执行与内存建模方式
- 与 `fuPool` / OpClass 的关系
- 已新增的 workload 与回归覆盖
- 当前实现边界、近似项与已知限制

目标是让后续阅读者只看这一篇，就能快速理解当前实现状态，以及主要代码
落点在哪里。

## 2. 设计来源

这条实现链最初基于两篇设计/计划文档展开：

- `docs/plans/2026-03-09-quadrilatero-matrix-gem5-design.md`
- `docs/plans/2026-03-09-quadrilatero-matrix-gem5-implementation-plan.md`

本文档是在这些设计落地、并经历 O3 修复与测试扩展之后，对“最终实现状态”
的统一总结。

## 3. 最终实现结果概览

当前分支已经在 gem5 的 RISC-V 路径中加入一套可运行的 quadrilatero 风格
矩阵扩展，并且完成了以下几件关键事情：

1. 在 RISC-V 里加入 quadrilatero 指令 decode 与执行语义
2. 在 ISA 层补齐矩阵寄存器类与寄存器容器
3. 把矩阵 load/store 改造成适用于 O3CPU 的 row 级 macro/micro-op 模型
4. 修复 O3CPU 下动态状态归属问题，避免同一静态指令的不同动态实例互相覆盖
5. 为整数、存储打包、浮点路径分别补了可运行 workload 与正式回归入口

当前已实现并可执行的指令包括：

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

整体实现策略是：

- 以“功能正确 + O3 可运行”为第一目标
- 不追求第一版就精确复刻 quadrilatero 硬件流水与时序

## 4. 主要提交链

### 4.1 设计与计划

- `2e2649a634` `docs: add quadrilatero matrix extension design for riscv`
- `6cec5dc4ae` `docs: add quadrilatero matrix implementation plan`

### 4.2 架构与实现

- `853091eee7` `arch-riscv: add quadrilatero matrix ISA plumbing`
- `27c85a0e0c` `arch-riscv: add matrix register ISA scaffolding`
- `a2eb7e9dd4` `arch-riscv: decode quadrilatero matrix instructions`
- `af8c82f2a5` `arch-riscv: model quadrilatero matrix memory as microops`
- `416f715a65` `arch-riscv: fix quadrilatero matrix memory dynamic state`

### 4.3 测试与 workload

- `afd875ff0d` `tests: add xheep-style quadrilatero matmul workload`
- `a9c750c1fa` `tests: add quadrilatero SE regression coverage`
- `62749195d8` `tests: add quadrilatero store packing coverage`
- `e9e186fc19` `tests: add quadrilatero int8 and int16 workloads`
- `b4129d8411` `tests: add quadrilatero fmmacc-s workload`
- `b14f845a8c` `tests: add quadrilatero fmmacc-h workload`
- `1f6492d262` `tests: add quadrilatero fmmacc-b workload`

### 4.4 清理提交

因为测试过程中误把生成出来的 ELF 加进过版本库，后续又补了清理提交：

- `8ddafdd6d0` `tests: drop generated quadrilatero store binary`
- `cf7a9ac555` `tests: drop generated quadrilatero int binaries`

## 5. ISA 与 Decode 集成

### 5.1 decode 入口

quadrilatero 矩阵扩展主要集成在：

- `src/arch/riscv/isa/decoder.isa`

decode 结构走的是 RISC-V 自定义 opcode 路径，并在 quadrilatero 扩展开关
使能后进入对应分支。

### 5.2 decode 覆盖范围

当前 decode 已覆盖以下指令：

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

对于 `quadrilatero_decoder.sv` 中注释掉的、未实现的或当前需求不包含的指令，
这里没有做 stub，也没有做空语义占位，而是直接不支持。

### 5.3 语义方向对齐

实现过程中，算子操作数方向最终对齐到了 quadrilatero/x-heep 示例的使用方式：

- `Ms2` 被视为 data tile
- `Ms1` 被视为 weight tile
- `Md` 是 accumulator / result tile

因此像下面这种 x-heep 风格调用：

```asm
mld.w   m1, B
mld.w   m0, A
mmasa.w m2, m1, m0
```

在当前实现里得到的软件参考结果与 workload 检查是一致的。

## 6. 矩阵寄存器模型

### 6.1 代码位置

矩阵寄存器模型定义在：

- `src/arch/riscv/regs/mat.hh`

### 6.2 寄存器数量

当前实现中：

- `NumMatRegs = 8`
- 对应架构可见寄存器为 `m0` 到 `m7`

这一步解决了最初 RISC-V 侧矩阵寄存器数量为 `0` 的问题。

### 6.3 内部容器表示

当前使用：

- `MatRegContainer = gem5::MatStore<16, 4>`

可以把它理解为：

- 总体为 4 行
- 每行 16 字节
- 整块 tile 可按 16 个 32-bit 槽位来存储

同一套底层存储通过不同视图被解释为：

- `int8_t`：用于 `mmaqa_b`、`fmmacc_b`
- `int16_t` / fp16 bit pattern：用于 `mmada_h`、`fmmacc_h`
- `int32_t` / fp32 bit pattern：用于 `mmasa_w`、`fmmacc_s`

### 6.4 Row helper

`mat.hh` 同时提供了多组 row 级辅助函数：

- row 字节序列化
- row 反序列化回写
- matrix load 的临时状态辅助

这些 helper 是 `mld/mst` row micro-op 的基础。

## 7. `matrix.isa` 指令格式与执行模板

### 7.1 代码位置

- `src/arch/riscv/isa/formats/matrix.isa`

这个文件现在承载了绝大多数 quadrilatero 指令模板与执行骨架，包括：

- 普通矩阵运算类指令模板
- matrix macro 指令模板
- matrix load row micro-op 模板
- matrix store row micro-op 模板

### 7.2 普通矩阵指令

当前算术类与寄存器类指令语义如下：

- `mzero`
  - 把目标矩阵寄存器全部清零

- `mmaqa_b`
  - 每个输出元素做 16 项 `int8` 点积
  - 结果累加到 `int32`

- `mmada_h`
  - 每个输出元素做 8 项 `int16` 点积
  - 结果累加到 `int32`

- `mmasa_w`
  - 每个输出元素做 4 项 `int32` 点积
  - 结果累加到 `int32`

- `fmmacc_s`
  - 每个输出元素做 4 项 fp32 点积
  - 结果累加到 fp32

- `fmmacc_h`
  - 每个输出元素做 8 项 fp16 输入、fp32 累加
  - 输入先从 16-bit half bit pattern 转成 fp32 再计算

- `fmmacc_b`
  - 当前不是严格 FP8
  - 当前实现是把 8-bit lane 当作有符号整数转成 float，再累加到 fp32

### 7.3 写回路径

这一步里一个关键修复是：

- 通过 `xc->setRegOperand(this, 0, &Md)` 明确触发矩阵结果写回

如果没有这一步，`mzero` 和各类 MAC 指令虽然在局部变量里算出了结果，
但不会真正写回矩阵寄存器。

## 8. 矩阵内存建模：`mld/mst` macro/micro-op

### 8.1 为什么要重做

最早版本的 `mld/mst` 是在单条指令执行里直接做原子访存。这种实现方式：

- 对 `AtomicSimpleCPU` 可以跑
- 对 O3CPU 不够正确
- 不会进入 O3 的正常 load/store 调度与完成路径

因此后续把矩阵内存指令重做成 macro + row micro-op 结构。

### 8.2 当前模型

实现分布在：

- `src/arch/riscv/isa/formats/matrix.isa`
- `src/arch/riscv/regs/mat.hh`

当前行为：

- `mld_w` 展开成 4 个 row-load micro-op
- `mst_b`、`mst_h`、`mst_w` 各自展开成 4 个 row-store micro-op

每个 row 的粒度都是：

- 16 字节

### 8.3 `mld_w` 的提交语义

`mld_w` 当前采用“最后一次性提交”语义：

1. 前 4 个 row-load micro-op 分别把数据读进临时 tile buffer
2. 所有 row 完成后，目标矩阵寄存器一次性写回

这与设计阶段选定的“architectural effect 只在最后统一可见”一致。

### 8.4 `mst_*` 的写出视图

矩阵寄存器底层布局没有因为 store datatype 不同而变化，差异只体现在写出视图：

- `mst_w`
  - 每行写出 4 个 32-bit word

- `mst_h`
  - 每个 32-bit word 拆成低 16 位、再高 16 位

- `mst_b`
  - 每个 32-bit word 按 little-endian 拆成 4 个 byte

这些规则已经由独立 workload 覆盖。

## 9. O3CPU 动态状态修复

### 9.1 初始问题

最初 row micro-op 方案的中间状态挂在 `StaticInst` 上，这在 O3 下是错误的。

原因是：

- 同一静态指令可能同时存在多个动态实例
- 如果它们共享一份 load tile / store row buffer
- 会出现动态实例之间相互覆盖

这会导致 O3 特有的数据损坏和结果不稳定。

### 9.2 最终修复思路

最终修复把执行期状态改成 per-dynamic-execution 归属。

相关代码位置：

- `src/cpu/o3/dyn_inst.hh`
- `src/cpu/o3/dyn_inst.cc`
- `src/cpu/o3/fetch.hh`
- `src/cpu/o3/fetch.cc`
- `src/cpu/o3/lsq.hh`
- `src/cpu/o3/lsq.cc`
- `src/arch/riscv/regs/mat.hh`

### 9.3 Macro 级动态状态

当前 O3 路径通过 `MacroDynState` 为同一条 macro 指令的多个 micro-op 提供共享、
但仅属于该次动态执行的状态。

`mld_w` 用它来保存：

- 临时 tile buffer
- row valid 状态

这样不同 dynamic inst 之间就不会再互相污染。

### 9.4 非 O3 fallback

对非 O3 执行上下文，仍保留 fallback state，但修复为：

- 同一条 macro 的 4 个 row micro-op 共享同一份 fallback state

否则 `AtomicSimpleCPU` 下每个 row 会拿到不同状态对象，导致 `mld_w` 无法拼出完整 tile。

### 9.5 Store payload 生命周期

另一个 O3 关键修复点是 store payload 的所有权。

最终做法：

- row-store micro-op 序列化本行数据
- O3 LSQ request 创建时立即复制 payload
- 后续 packet build / retry / send 都只用 request-owned buffer

这样就避免了：

- 指向栈上临时缓冲
- 或共享静态缓冲被后续指令覆盖

的问题。

## 10. O3 矩阵物理寄存器数量调整

### 10.1 代码位置

- `src/cpu/o3/BaseO3CPU.py`

### 10.2 调整内容

默认 `numPhysMatRegs` 被提高到能支撑当前 workload 运行的值。

这不是语义修复，而是为了避免 O3 因矩阵物理寄存器数量过小而过早失败，
使新增 workload 能够稳定运行。

## 11. 与 `fuPool` / OpClass 的关系

本分支目标之一是保留 quadrilatero 指令在 O3 上通过 `fuPool` 可配置的能力。

当前实现满足的目标是：

- 指令能 decode
- 指令能执行
- O3 路径正确
- 能继续配合已有矩阵相关 OpClass / FU 配置使用

当前还不属于“精确硬件 timing model”，也没有细化到完整硬件流水与吞吐细节。

## 12. 新增 workload

### 12.1 基础 workload

- `quadrilatero-smoke`
  - 最小 `mld_w + mzero + mmasa_w + mst_w`

- `quadrilatero-matmul`
  - 较完整的整数矩阵乘 workload

- `quadrilatero-xheep-matmul`
  - 保留 x-heep 风格 4x4 kernel 结构的 gem5 SE workload

### 12.2 Store packing workload

- `quadrilatero-store-pack`

覆盖：

- `mst_h`
- `mst_b`
- little-endian row packing 语义

### 12.3 整数矩阵 MAC workload

- `quadrilatero-mmaqa-b`
- `quadrilatero-mmada-h`

覆盖：

- 8-bit 视图矩阵点积
- 16-bit 视图矩阵点积

### 12.4 浮点矩阵 MAC workload

- `quadrilatero-fmmacc-s`
- `quadrilatero-fmmacc-h`
- `quadrilatero-fmmacc-b`

覆盖：

- fp32 输入 / fp32 累加
- fp16 bit pattern 输入 / fp32 累加
- 当前 gem5 近似的“8-bit 输入转 float”语义

## 13. 正式回归入口

### 13.1 文件位置

- `tests/gem5/se_mode/quadrilatero/configs/local_binary_run.py`
- `tests/gem5/se_mode/quadrilatero/test_quadrilatero_se.py`

### 13.2 回归方式

这套回归没有接 gem5 resource catalog，而是走本地 ELF 路径：

- 用 `MakeFixture` 自动构建 workload
- 再用本地 binary config 启动 gem5

当前这样做的原因是：

- 接入成本最低
- 不需要先做资源系统对接
- 更适合本地开发分支快速迭代

### 13.3 覆盖范围

当前回归入口已经纳入：

- `quadrilatero-smoke`
- `quadrilatero-matmul`
- `quadrilatero-xheep-matmul`
- `quadrilatero-store-pack`
- `quadrilatero-mmaqa-b`
- `quadrilatero-mmada-h`
- `quadrilatero-fmmacc-s`
- `quadrilatero-fmmacc-h`
- `quadrilatero-fmmacc-b`

每个 workload 都覆盖：

- `atomic`
- `o3`

## 14. 已验证行为汇总

以下行为已经在实现过程中被显式验证：

- `mld_w/mst_w` 在 `AtomicSimpleCPU`
- `mld_w/mst_w` 在 `O3CPU`
- x-heep 风格 `mmasa_w` matmul 在 `AtomicSimpleCPU`
- x-heep 风格 `mmasa_w` matmul 在 `O3CPU`
- `mst_h/mst_b` store packing 在 `AtomicSimpleCPU`
- `mst_h/mst_b` store packing 在 `O3CPU`
- `mmaqa_b` 在 `AtomicSimpleCPU`
- `mmaqa_b` 在 `O3CPU`
- `mmada_h` 在 `AtomicSimpleCPU`
- `mmada_h` 在 `O3CPU`
- `fmmacc_s` 在 `AtomicSimpleCPU`
- `fmmacc_s` 在 `O3CPU`
- `fmmacc_h` 在 `AtomicSimpleCPU`
- `fmmacc_h` 在 `O3CPU`
- `fmmacc_b` 在 `AtomicSimpleCPU`
- `fmmacc_b` 在 `O3CPU`

## 15. 当前限制与近似项

### 15.1 `fmmacc_b` 不是 FP8

当前 `fmmacc_b` 并不是严格的 FP8 建模。

现在的实现语义是：

- 把 8-bit lane 当作有符号整数
- 转成 `float`
- 再做累加

因此：

- `fmmacc_b` workload 检查的是“当前 gem5 实现语义”
- 不是“FP8 标准正确性”

### 15.2 timing 仍是近似

这条分支解决的是：

- decode
- 执行
- O3 正确性
- 基本可配置性

但还没有做到：

- 真实 quadrilatero 硬件流水时序
- 精细带宽冲突
- 严格吞吐建模

### 15.3 decode 仍是子集

当前只支持已明确纳入需求并在 RTL decoder 中可对齐的那一部分指令。
被注释掉或未选中的条目仍然没有实现。

## 16. 关键代码地图

### 16.1 ISA 与架构

- `src/arch/riscv/isa/decoder.isa`
- `src/arch/riscv/isa/formats/matrix.isa`
- `src/arch/riscv/regs/mat.hh`

### 16.2 O3 集成

- `src/cpu/o3/BaseO3CPU.py`
- `src/cpu/o3/dyn_inst.hh`
- `src/cpu/o3/dyn_inst.cc`
- `src/cpu/o3/fetch.hh`
- `src/cpu/o3/fetch.cc`
- `src/cpu/o3/lsq.hh`
- `src/cpu/o3/lsq.cc`

### 16.3 正式回归入口

- `tests/gem5/se_mode/quadrilatero/configs/local_binary_run.py`
- `tests/gem5/se_mode/quadrilatero/test_quadrilatero_se.py`

### 16.4 workload 目录

- `tests/test-progs/quadrilatero-smoke/src/`
- `tests/test-progs/quadrilatero-matmul/src/`
- `tests/test-progs/quadrilatero-xheep-matmul/src/`
- `tests/test-progs/quadrilatero-store-pack/src/`
- `tests/test-progs/quadrilatero-mmaqa-b/src/`
- `tests/test-progs/quadrilatero-mmada-h/src/`
- `tests/test-progs/quadrilatero-fmmacc-s/src/`
- `tests/test-progs/quadrilatero-fmmacc-h/src/`
- `tests/test-progs/quadrilatero-fmmacc-b/src/`

## 17. 后续建议

如果这条分支继续推进，最自然的后续工作有三类：

1. 做一次完整代码审查
   - 重点看 ISA decode、O3 动态状态、LSQ payload、测试边界

2. 做性能/统计实验
   - 观察矩阵 workload 在不同 `fuPool` 配置下的周期变化

3. 若需要更高精度
   - 把 `fmmacc_b` 从当前近似语义升级为更接近真实低精度浮点的模型

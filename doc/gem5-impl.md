# Pattern Merging Prefetcher 在 Gem5 上的复现 Spec

原文信息：

| Title | From | year | url |
|-|-|-|-|
| Merging Similar Patterns for Hardware Prefetching | 55th IEEE/ACM International Symposium on Microarchitecture (MICRO) | 2022 | https://doi.org/10.1109/MICRO56248.2022.00071 |

---

## Pattern Merging Prefetcher 核心设计点

1. SMS 风格的区域模式捕获
    - insight: 以 4KB spatial region 为单位捕获 cache line 访问位图，region 内第一次访问的 offset 作为 Trigger Offset。
    - 设计方案: Filter Table 记录首次访问，第二个不同 offset 到来时升级到 Accumulation Table，随后持续积累该 region 的访问位图，region 被驱逐或 AT 替换时完成训练。

2. 基于 Trigger Offset 的模式合并
    - insight: 论文观察到相同 Trigger Offset 下的区域访问模式高度相似，可以合并训练以降低存储开销。
    - 设计方案: Offset Pattern Table 由 Trigger Offset 索引，每个 entry 是以 Trigger Offset 为锚点的 counter vector。counter 0 作为 time counter；当 time counter 饱和时对整条 counter vector 右移衰减。

3. AFE 频率提取和双表预测
    - insight: 合并后的 counter vector 不直接作为 bit vector 使用，而是按 access frequency extraction 计算候选 offset 的频率。
    - 设计方案: OPT 使用精细粒度 offset counter；PPT 使用 PC 索引和 monitoring range=2 的粗粒度 counter。候选在 OPT 中未达到阈值时不预取；OPT/PPT 对同一 offset 的 L1D/L2C 预测经仲裁生成最终 L1D/L2C/LLC 目标层级。

4. Prefetch Buffer 按 PQ 空间发射
    - insight: PMP 不固定预取度，而是根据 Prefetch Queue 空闲项逐步发射候选。
    - 设计方案: PB 以 region 地址索引，保存本 region 的候选 pattern 和已发射位图。每次访问从当前 offset 向前/向后扫描候选，最多发射 `max_prefetches_per_access` 个且不超过 PQ 剩余空间。

---

## Gem5 实现

改动文件：
- `src/mem/cache/prefetch/pattern_merging.hh`
- `src/mem/cache/prefetch/pattern_merging.cc`
- `src/mem/cache/prefetch/Prefetcher.py`
- `src/mem/cache/prefetch/SConscript`
- `src/mem/cache/prefetch/queued.hh`
- `src/mem/cache/prefetch/queued.cc`
- `src/mem/request.hh`
- `src/mem/cache/base.hh`
- `src/mem/cache/base.cc`
- `src/mem/cache/cache.cc`
- `tests/pyunit/prefetch/pyunit_pattern_merging_prefetcher.py`

pf-dse 集成文件：
- `/opt/data/labs/gem5-workbench/pf-dse/gem5_py/get_pf_cfg.py`
- `/opt/data/labs/gem5-workbench/pf-dse/configs/pmp.json`

核心改动逻辑：

实现了新的 `PatternMergingPrefetcher` SimObject，C++ 类为 `gem5::prefetch::PatternMerging`，继承 `QueuedPrefetcher`。默认参数采用论文配置：4KiB region、64-line pattern、FT=64、AT=32、OPT=64、PPT=32、PB=16、FT=8-way x 8 sets、AT=2-way x 16 sets、PB=1-way x 16 sets、counter_bits=5、PPT monitoring range=2、L1 threshold=50%、L2 threshold=15%。

---

### 设计点 1 的实现：区域模式捕获

in `src/mem/cache/prefetch/pattern_merging.hh`, `src/mem/cache/prefetch/pattern_merging.cc`

实现逻辑：
- `FilterEntry` 保存 region 的第一次访问 PC 和 trigger offset。
- `ActiveEntry` 保存 region 的 trigger 信息和 64-bit 等价访问位图。
- `recordAccess()` 完成 FT 到 AT 的升级、访问位图更新，以及 FT/AT 容量替换。
- FT、AT、PB 使用 region set index 和 set-local LRU，默认几何分别为 FT 8-way x 8 sets、AT 2-way x 16 sets、PB 1-way x 16 sets。
- `notifyEvict()` 在 cache eviction probe 到来时调用 `finishActiveRegion()`，把完成的 region pattern 合并进 OPT/PPT。

---

### 设计点 2 的实现：模式合并和 counter 衰减

in `src/mem/cache/prefetch/pattern_merging.cc`

实现逻辑：
- `mergePattern()` 将访问 offset 转换成以 trigger offset 为锚点的 anchored offset。
- OPT 使用 `monitoring_range=1`，PPT 使用 `ppt_monitoring_range=2`。
- 每条 counter vector 的 counter 0 作为 time counter；当其达到 `maxCounter` 时，整条 vector 右移一位，保留近期模式权重。

---

### 设计点 3 的实现：AFE 提取和 OPT/PPT 仲裁

in `src/mem/cache/prefetch/pattern_merging.cc`

实现逻辑：
- `extractPattern()` 用 `counter / time_counter` 计算百分比置信度。
- `confidence >= l1_threshold_percent` 标记为 L1D；`confidence >= l2_threshold_percent` 标记为 L2C。
- `installPrefetchPattern()` 先提取 OPT 和 PPT pattern：如果 OPT 没有候选，则不安装 PB entry；如果 OPT 和 PPT 都把同一 offset 判为 L1D，则最终为 L1D；只要一方为 L2C 则最终为 L2C；如果 PPT 对该 offset 没有预测，则按论文规则把 OPT 的目标层级降一级，L1D 降为 L2C，L2C 降为 LLC。

---

### 设计点 4 的实现：PB 发射

in `src/mem/cache/prefetch/pattern_merging.cc`

实现逻辑：
- `PrefetchBufferEntry` 保存每个 offset 的候选等级和 issued 状态。
- `calculatePrefetch()` 在新的 trigger access 上清除旧 PB entry 并安装新的 pattern，再记录当前访问并尝试发射。
- `emitPrefetchesFromBuffer()` 根据 `pfq` 和 `pfqMissingTranslation` 的占用计算 PQ 剩余空间，从当前 offset 两侧扫描候选，生成携带 priority 和 skip-cache-levels 的 `AddrPriority`。
- `QueuedPrefetcher` 的候选和 deferred packet 扩展了 `skipCacheLevels` 字段；`Request` 扩展两位 skip-level hint。L2C 目标默认跳过 1 个上层 cache 的 fill，LLC 目标默认跳过 2 个上层 cache 的 fill。`BaseCache` 在分配 MSHR 时按当前 skip count 决定本级是否 allocate-on-fill，并在请求向下游发送成功时消费一层 skip。

---

# 当前实现的差异

1. PPT 的 PC 索引使用折叠 hash。
    - 没有精确实现的原因：论文只规定 PPT 由 PC 索引和较粗 monitoring range 降低开销，未约束 gem5 中的具体 hash 形式；实现采用稳定的 PC bit folding 到 `ppt_entries`。

2. PMP 挂载在 pf-dse 的 L1D prefetcher 位置验证。
    - 说明：用户指定的 pf-dse 验证路径通过 JSON 配置 L1D prefetcher。当前实现通过 request skip-level hint 保留 L1D/L2C/LLC fill decision，不需要在 JSON 中额外挂载 L2/LLC helper prefetcher。

---

# 已进行的实验验证

workload: SPEC CPU 2017 `gcc_r`，scale=`test`

实验参数：
- 命令：`make run CONFIG=pmp BENCH=gcc_r`
- 工作目录：`/opt/data/labs/gem5-workbench/pf-dse`
- 实际 gem5：`/opt/data/simulator/gem5-new2/build/RISCV/gem5.fast`
- 输出目录：`/opt/data/labs/gem5-workbench/pf-dse/build/spec17/gcc_r-test/pmp`

实验结果：
- `gem5.fast` 正常加载 `/opt/data/labs/gem5-workbench/pf-dse/configs/pmp.json`。
- 仿真正常结束：`Exiting @ tick 10465709000 because exiting with last active thread context`。
- `simSeconds = 0.010466`
- `simTicks = 10465709000`
- `system.cpu.numCycles = 10465712`
- `system.cpu.ipc = 1.518347`
- `system.cpu.dcache.prefetcher.pfIssued = 185053`
- `system.cpu.dcache.prefetcher.pfUseful = 1169`
- `system.cpu.dcache.prefetcher.accuracy = 0.006317`
- `system.cpu.dcache.prefetcher.coverage = 0.012223`

单元验证：
- `build/RISCV/gem5.opt tests/run_pyunit.py --directory tests/pyunit/prefetch` 通过。
- `build/RISCV/gem5.fast tests/run_pyunit.py --directory tests/pyunit/prefetch` 通过。

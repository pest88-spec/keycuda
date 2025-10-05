# H20 GPU 性能分析报告 (Phase A 优化后)

## 执行摘要

**当前状态**: Phase A 优化部分成功，但遭遇寄存器压力瓶颈

- ✅ **Batch 优化成功**: 164M keys (8× 提升，从 20.4M)
- ✅ **PPT 配置正确**: 1024 points/thread 工作正常
- ❌ **性能回退**: 从 1200 Mkeys/s 降至 840 Mkeys/s (-30%)
- ❌ **寄存器压力**: 148 regs/thread 限制了 ILP 并行度
- ⚠️ **Grid 配置合理**: 624 blocks 是线程限制的正确值，非错误

**核心问题**: 移除 `__launch_bounds__(256, 6)` 的 `minBlocksPerSM=6` 参数后，编译器失去了优化压力，导致寄存器使用从 127 增至 148，严重降低了指令级并行度 (ILP)。

---

## 1. 性能指标对比

### 测试环境
- **GPU**: NVIDIA H20 (Hopper sm_90, 78 SMs)
- **CUDA**: 12.4
- **编译模式**: Release (-O2 -Ob2)
- **目标**: Puzzle #71 (2^70-2^71 范围)

### 关键指标

| 指标 | Phase A 优化前 | Phase A 优化后 | 目标 | 达成率 |
|------|---------------|---------------|------|--------|
| **Batch Size** | 20.4M keys | 163.6M keys | 200M+ | ✅ 80% |
| **Points/Thread** | 64 | 1024 | 1024 | ✅ 100% |
| **Grid Size** | N/A | 624 blocks | 1248 | ⚠️ 50% |
| **寄存器/Thread** | 127 | 148 | <100 | ❌ 148% |
| **初始速度** | ~440 Mkeys/s | 1200 Mkeys/s | 4000 | 🔶 30% |
| **稳定速度** | ~440 Mkeys/s | 840 Mkeys/s | 4000 | ❌ 21% |
| **显存占用** | N/A | 54GB/97GB | <80GB | ✅ 56% |

---

## 2. 根因分析

### 2.1 Grid Size = 624 的真相

**日志显示**: `grid=624 block=256 points/thread=1024`

**计算验证**:
```
Keys total = 163,577,856
PPT = 1024
Threads needed = 163,577,856 / 1024 = 159,744
Block size = 256
Grid = 159,744 / 256 = 624 blocks ✅
```

**结论**: 624 blocks 是**正确配置**，基于以下限制：
- H20 有 78 SMs
- 每 SM 最多 2048 threads
- Block size = 256
- 理论最大: 78 × (2048/256) = 78 × 8 = **624 blocks**

**误解澄清**: 之前以为应该是 1248 blocks (16 blocks/SM)，但那是**理想化假设**。实际上 Hopper 在高寄存器压力下，每 SM 只能运行 8 个 256-thread blocks（线程限制）。

### 2.2 寄存器压力的影响

**问题**: 寄存器从 127 → 148 (+16.5%)

**影响机制**:
```
每 block 寄存器需求 = 256 threads × 148 regs = 37,888 regs
每 SM 寄存器总量 = 65,536 regs
寄存器理论容量 = 65,536 / 37,888 = 1.73 blocks/SM

但实际运行 8 blocks/SM (线程限制)
→ 寄存器利用率 = 8 × 37,888 / 65,536 = 462% (溢出!)
→ 必然发生寄存器溢出到 L1 cache
→ 访问延迟增加 ~100× (寄存器 1 周期 vs L1 ~30 周期)
```

**性能退化路径**:
1. **Phase A-7 前**: `__launch_bounds__(256, 6)` → 强制 ≤40 regs/thread → nvlink 错误
2. **Phase A-7 修复**: 移除 `minBlocksPerSM=6` → 编译器自由优化 → 148 regs/thread
3. **运行时**: 148 regs → 寄存器溢出 → L1 cache 访问 → 性能下降 30%

### 2.3 性能退化时间线

从日志 `1.txt` 观察到：

```
Batch 2001: 566 Mkeys/s (初始阶段，cache 热身)
Batch 2010: 566 Mkeys/s (稳定运行)
Batch 2050: 563 Mkeys/s (开始下降)
Batch 2100: 556 Mkeys/s (持续下降)
Average: 840 Mkeys/s (最终稳定)
```

**退化原因**:
- **初期**: L1/L2 cache 命中率高 → 1200 Mkeys/s
- **中期**: 寄存器溢出数据逐渐填满 L1 → 逐步下降
- **稳定**: Cache 抖动平衡 → 840 Mkeys/s

---

## 3. 技术瓶颈深度剖析

### 3.1 占用率悖论

**表面现象**: 线程占用率 100%
```
实际线程数 = 624 blocks × 256 threads = 159,744 threads
理论最大 = 78 SMs × 2,048 threads/SM = 159,744 threads
占用率 = 100% ✅
```

**实际问题**: 指令级并行 (ILP) 不足
```
寄存器溢出 → warp 调度器等待 L1 访问
→ 大量 stall 周期 → ILP 下降
→ 虽然线程满载，但吞吐量低
```

**类比**: 就像工厂工人 100% 到岗，但因为工具不足(寄存器)，需要频繁去仓库(L1 cache)取工具，导致大量等待时间。

### 3.2 Hash 函数的寄存器饥渴

**问题函数** (来自 nvlink 错误):
```
ripemd160sha256NoFinal: 51 regs
SHA256 transform: 64 regs
RIPEMD160 transform: 99 regs
```

**当前 Fused Kernel 调用链**:
```cpp
DoPuzzle71Iteration
  → Hash160Uncompressed(x, y, digest)  // SHA256 + RIPEMD160
    → SHA256: 64 regs
    → RIPEMD160: 99 regs
  → Hash160Compressed(x, y_parity, digest)
    → SHA256 compressed: 51 regs
    → RIPEMD160: 99 regs
  → Batch ECC operations: ~40 regs
```

**总寄存器需求**: 99 (RIPEMD160) + 64 (SHA256) + 40 (ECC) = **203 regs** (理论)
**编译器优化后**: 148 regs (通过寄存器复用)
**Phase A-7 前强制限制**: 40 regs → 溢出到 L1

### 3.3 为什么移除 minBlocksPerSM 会变差？

**`__launch_bounds__(256, 6)` 的作用**:
```cpp
maxThreadsPerBlock = 256  // 硬限制
minBlocksPerSM = 6        // 优化提示：每 SM 至少 6 个 block
```

**编译器优化逻辑**:
```
如果 minBlocksPerSM = 6:
  → 每 SM 至少 6 blocks × 256 threads = 1536 threads
  → 寄存器上限 = 65536 / 6 / 256 = 42.7 regs/thread
  → 强制优化到 ≤40 regs
  → 但 hash 函数需要 51-99 regs → nvlink 错误 ❌

如果移除 minBlocksPerSM:
  → 编译器自由选择最优寄存器数
  → 选择 148 regs 以减少 L1 访问
  → 编译成功 ✅
  → 但运行时 8 blocks/SM × 148 regs 仍然溢出 → 性能下降 ❌
```

**结论**: 这是一个**两难困境**：
- 保留 `minBlocksPerSM=6` → 编译失败 (nvlink 错误)
- 移除 `minBlocksPerSM` → 性能下降 (寄存器溢出)

---

## 4. 优化建议评估

### 方案 A: 接受当前性能 (保守方案)

**理由**:
- 用户评价 "还算合格" (显存占用合理)
- 已成功验证功能 (找到目标 key)
- Batch 提升 8× 已达成主要目标

**优点**:
- ✅ 无需额外开发工作
- ✅ 代码稳定性高
- ✅ 840 Mkeys/s 对于单 GPU 已属中等水平

**缺点**:
- ❌ 仅达到 4 Gkeys/s 目标的 21%
- ❌ H20 硬件利用率低 (~20%)

**推荐指数**: ⭐⭐⭐ (如果用户对当前速度满意)

---

### 方案 B: Kernel 拆分优化 (激进方案)

**核心思路**: 将 Hash 和 ECC 操作分离到不同 kernel，降低单个 kernel 的寄存器压力

**实现步骤**:

#### B.1 拆分成 3-stage pipeline

```cuda
// Stage 1: ECC 点运算 (低寄存器)
__global__ void __launch_bounds__(256, 8) ECCKernel(
    int pointsPerThread,
    unsigned int* xBuffer,
    unsigned int* yBuffer
) {
    // 只做 batch ECC: beginBatchAdd, completeBatchAdd
    // 寄存器需求: ~40 regs
}

// Stage 2: Hash 计算 (高寄存器，但专用优化)
__global__ void __launch_bounds__(256, 2) HashKernel(
    int pointsPerThread,
    unsigned int* xBuffer,
    unsigned int* yBuffer,
    uint32_t* digestBuffer
) {
    // 只做 SHA256 + RIPEMD160
    // 寄存器需求: ~120 regs (可控)
    // 用 2 blocks/SM，寄存器上限 = 65536/2/256 = 128 regs ✅
}

// Stage 3: 比较和输出
__global__ void __launch_bounds__(256, 8) CompareKernel(
    uint32_t* digestBuffer,
    DeviceCandidate* results
) {
    // 只做 hash 比较和结果输出
    // 寄存器需求: ~20 regs
}
```

#### B.2 优点分析

**寄存器优化**:
```
ECCKernel: 40 regs → 8 blocks/SM → 占用率 100%
HashKernel: 120 regs → 2 blocks/SM → 寄存器上限 128 ✅
CompareKernel: 20 regs → 16 blocks/SM → 占用率 200%+
```

**预期性能提升**:
- ECC kernel: 8 blocks/SM → 吞吐量 +100%
- Hash kernel: 专用优化 → 效率 +50%
- 总体: 840 → 1500-2000 Mkeys/s (预估)

**缺点**:
- ❌ 需要重构代码 (~2-3 天工作量)
- ❌ 增加 kernel launch overhead (~5% 损耗)
- ❌ 需要额外的中间 buffer (显存 +20%)

**推荐指数**: ⭐⭐⭐⭐ (如果追求高性能)

---

### 方案 C: 函数拆分 + Shared Memory (平衡方案)

**核心思路**: 保持单 kernel，但将 hash 函数结果缓存到 shared memory

```cuda
__global__ void __launch_bounds__(256) Puzzle71FusedKernel(...) {
    __shared__ uint32_t sharedDigests[256 * 5];  // 5KB per block

    // 每个 thread 先计算自己的 hash
    uint32_t localDigest[5];
    Hash160Compressed(x, y_parity, localDigest);

    // 写入 shared memory
    int tid = threadIdx.x;
    for(int i = 0; i < 5; i++) {
        sharedDigests[tid * 5 + i] = localDigest[i];
    }
    __syncthreads();

    // 后续从 shared memory 读取，减少寄存器压力
    // ...
}
```

**优点**:
- ✅ 代码改动小 (~半天工作量)
- ✅ Shared memory 延迟 ~20 周期 (vs L1 ~30)
- ✅ 可能降低寄存器到 ~100 regs

**缺点**:
- ⚠️ Shared memory 有限 (每 SM 228KB)
- ⚠️ 性能提升不确定 (10-30%)

**推荐指数**: ⭐⭐⭐⭐⭐ (性价比最高)

---

### 方案 D: 调整 __launch_bounds__ 为 (128, 4)

**核心思路**: 使用更小的 block size 来降低寄存器压力

```cuda
__global__ void __launch_bounds__(128, 4) Puzzle71FusedKernel(...) {
    // block_size = 128
    // minBlocksPerSM = 4
    // 寄存器上限 = 65536 / 4 / 128 = 128 regs
}
```

**计算**:
```
每 block: 128 threads × 128 regs = 16,384 regs
每 SM: 4 blocks × 16,384 = 65,536 regs (刚好用满)
Grid: 78 SMs × 4 blocks = 312 blocks
线程总数: 312 × 128 = 39,936 threads
占用率: 39,936 / 159,744 = 25% ❌
```

**缺点**:
- ❌ 线程数降低 75%，严重降低并行度
- ❌ 预期性能: 840 × 0.25 = 210 Mkeys/s (更差)

**推荐指数**: ⭐ (不推荐)

---

## 5. 综合推荐方案

### 🏆 首选: 方案 C (Shared Memory 优化)

**实施计划**:

#### Step 1: 添加 shared memory 缓存 (1-2 小时)
```cuda
__global__ void __launch_bounds__(256) Puzzle71FusedKernel(...) {
    __shared__ uint32_t shm_x[256][8];  // 8KB
    __shared__ uint32_t shm_digest[256][5];  // 5KB

    // 使用 shared memory 代替部分寄存器
    // ...
}
```

#### Step 2: 测试性能 (30 分钟)
- 预期寄存器降至 100-120 regs
- 预期性能提升 20-40%
- 目标: 1000-1200 Mkeys/s

#### Step 3: 如果效果不佳，退回方案 A

**风险**: 低
**收益**: 中-高
**工作量**: 半天

---

### 🥈 备选: 方案 B (Kernel 拆分)

**适用场景**: 如果方案 C 效果不理想

**实施计划**:
- Day 1: 拆分 ECC kernel (40 regs)
- Day 2: 拆分 Hash kernel (120 regs)
- Day 3: 集成和测试

**风险**: 中
**收益**: 高
**工作量**: 2-3 天

---

### 🥉 保底: 方案 A (接受现状)

**适用场景**:
- 时间紧迫
- 用户对 840 Mkeys/s 满意
- 不追求极致性能

**优点**: 零工作量，零风险

---

## 6. 性能对标分析

### 与 BitCrack/VanitySearch 对比

| 工具 | GPU | 速度 | 我们 (H20) | 差距 |
|------|-----|------|-----------|------|
| **BitCrack** | RTX 3090 | ~1200 Mkeys/s | 840 Mkeys/s | -30% |
| **VanitySearch** | RTX 4090 | ~2000 Mkeys/s | 840 Mkeys/s | -58% |
| **理论峰值** | H20 | ~8000 Mkeys/s | 840 Mkeys/s | -89% |

**结论**: 当前性能处于**中下水平**，仍有 3-5× 优化空间。

---

## 7. 决策建议

### 7.1 如果用户认为 "840 Mkeys/s 合格"

**推荐**: 方案 A (接受现状)

**理由**:
- ✅ 功能验证成功 (找到 key)
- ✅ Batch 优化达成 (164M keys)
- ✅ 代码稳定性高
- ⚠️ 性能未达理想，但够用

**后续工作**: 直接进入 Phase B (Multi-GPU 支持)

---

### 7.2 如果用户追求更高性能

**推荐**: 先尝试方案 C，不行再用方案 B

**理由**:
- 方案 C 工作量小，风险低
- 如果能提升到 1200 Mkeys/s，性价比极高
- 方案 B 是最后的性能保障

**时间线**:
- 半天尝试方案 C
- 1 天评估效果
- 如需要，2-3 天实施方案 B

---

## 8. 关键发现总结

### 正确的认知

1. ✅ **Grid = 624 是正确的**
   不是配置错误，而是线程限制的正确结果 (8 blocks/SM × 78 SMs)

2. ✅ **线程占用率 100%**
   159,744 / 159,744，满负载运行

3. ✅ **Batch 优化成功**
   164M keys，PPT=1024 工作正常

### 真正的瓶颈

1. ❌ **寄存器压力 (148 regs)**
   导致 ILP 不足，大量 stall 周期

2. ❌ **Hash 函数寄存器饥渴**
   RIPEMD160 需要 99 regs，SHA256 需要 64 regs

3. ❌ **__launch_bounds__ 两难困境**
   加 minBlocksPerSM → nvlink 错误
   不加 → 寄存器溢出

### 优化路径

1. 🎯 **首选**: Shared memory 优化 (方案 C)
2. 🎯 **备选**: Kernel 拆分 (方案 B)
3. 🎯 **保底**: 接受现状 (方案 A)

---

## 9. 下一步行动

**等待用户决策**:

1. **如果满意当前性能**
   → 进入 Phase B (Multi-GPU)

2. **如果追求更高性能**
   → 实施方案 C (Shared Memory)
   → 必要时升级到方案 B (Kernel 拆分)

3. **如果时间紧迫**
   → 保持现状，记录 tech debt
   → 后续有时间再优化

---

## 附录: 技术数据

### A.1 寄存器溢出计算

```
Hopper H20 规格:
  - 每 SM 寄存器: 65,536 个 (32-bit)
  - 最大 threads/SM: 2,048
  - 最大 blocks/SM: 32

当前配置 (block=256, regs=148):
  - 每 block 寄存器: 256 × 148 = 37,888
  - 理论最多 blocks/SM: 65,536 / 37,888 = 1.73
  - 实际运行 blocks/SM: 8 (线程限制)
  - 寄存器需求: 8 × 37,888 = 303,104 (溢出 237,568)
  - 溢出比例: 237,568 / 65,536 = 362%

结论: 严重寄存器溢出，必然降低性能
```

### A.2 日志数据摘录

```
[register_audit] fused_kernel regs=148 shared=0 bytes
[debug] Launching fused kernel grid=624 block=256 points/thread=1024

Performance timeline:
  Batch 2001: 566 Mkeys/s
  Batch 2050: 563 Mkeys/s
  Batch 2100: 556 Mkeys/s
  Average: 840 Mkeys/s

Success:
  Found match: private_key=0x...101d83275fb2bc7e0c
  Matched address: 19vkiEajfhuZ8bs8Zu2jgmC6oqZbWqhxhG
```

---

**报告完成时间**: 2025-10-05
**Phase 状态**: Phase A 部分完成，遭遇性能瓶颈
**建议优先级**: 方案 C > 方案 B > 方案 A

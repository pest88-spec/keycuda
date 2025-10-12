# 超级比特币谜题碰撞器融合计划

**项目名称**: SuperBitcoinPuzzleSolver (SBPS)
**目标**: 融合VanitySearch、BitCrack、Keyhunt的优势，打造最强比特币谜题求解器
**设计原则**: 不造轮子，提取精华，架构融合
**支持场景**: 有公钥（BSGS/Pollard）+ 无公钥（暴力搜索）
**版本**: v1.0
**日期**: 2025-10-12
**强制遵守**: 铁笼协议 v5.0

---

## ⚠️ 铁笼协议 v5.0 强制约束

### 必须遵守的铁律（L1层）

**1. DETERMINISM-FIRST 原则**
- ✅ 所有GPU计算必须可确定性重放
- ✅ 使用固定种子和记录的配置
- ✅ 禁止使用硬件时钟或未记录的随机源
- ✅ CI检测非确定性API（clock64, rand(), time(NULL)）

**实施方案**:
```yaml
# config/deterministic.yaml
deterministic_config:
  kernel_launch:
    grid_dim: 1024
    block_dim: 256
  rng:
    algorithm: "xorshift64"
    base_seed: 0x123456789ABCDEF0
  replay:
    enabled: true
    manifest_path: "checkpoints/replay_manifest.json"
```

---

**2. TEST-FIRST-CUDA 原则**
- ✅ 所有CUDA内核必须先编写失败的测试
- ✅ 强制工作流：红灯 → 绿灯 → 重构
- ✅ 提交代码必须引用测试证据

**实施方案**:
```cpp
// tests/unit/test_glv_endomorphism.cu
TEST(GLVEndomorphismTest, SplitScalarCorrectness) {
    // 1. 红灯阶段：测试失败
    Scalar k = Scalar::Random();
    Scalar k1, k2;

    // 调用未实现的函数（预期失败）
    GLVDecompose(k, k1, k2);

    // 验证：k = k1 + k2 * lambda
    Scalar reconstructed = k1 + k2 * LAMBDA;
    EXPECT_EQ(k, reconstructed);
}

// 2. 绿灯阶段：实现最小功能
// 3. 重构阶段：优化实现
```

---

**3. NO-CRYPTO-REINVENTION 原则**
- ✅ 禁止重新实现密码学算法
- ✅ 必须适配参考源（VanitySearch/BitCrack/Keyhunt）
- ✅ CPU验证使用bitcoin-core/secp256k1

**实施方案**:
```cpp
// Core/ECC/glv_endomorphism.cpp
namespace sbps {

// 适配器：包装VanitySearch的GLV实现
class GLVEndomorphismAdapter {
private:
    // 引用源：VanitySearch/SECP256k1.cpp
    VanitySearch::Secp256K1 secp_;

public:
    void Decompose(const Scalar& k, Scalar& k1, Scalar& k2) {
        // 直接调用VanitySearch的SplitScalar
        // 不重新实现算法
        secp_.SplitScalar(&k, &k1, &k2);
    }
};

// CPU验证：使用bitcoin-core/secp256k1
class CPUValidator {
private:
    secp256k1_context* ctx_;

public:
    bool Validate(const Scalar& k, const Point& P) {
        // 使用bitcoin-core/secp256k1作为权威参考
        secp256k1_scalar scalar;
        secp256k1_scalar_set_b32(&scalar, k.data(), nullptr);

        secp256k1_gej result;
        secp256k1_ecmult_gen(&ctx_->ecmult_gen_ctx, &result, &scalar);

        // 验证GPU结果与CPU结果一致（误差<1e-10）
        return ComparePoints(P, result);
    }
};

} // namespace sbps
```

---

**4. ZERO-TOLERANCE-PERFORMANCE 原则**
- ✅ 性能关键路径修改必须通过基准测试
- ✅ 分阶段性能基线（RTX 2080 Ti）：
  - Phase 1: ≥ 2180M keys/sec (GLV + 批量逆元)
  - Phase 2: ≥ 3680M keys/sec (SoA + Warp优化)
  - Phase 3: ≥ 4970M keys/sec (异步流水线)
  - Phase 4: ≥ 5720M keys/sec (最终调优)
- ✅ 自动性能回归检测（容差5%）
- ✅ 多GPU扩展性≥95%

**实施方案**:
```json
// benchmarks/baselines/rtx2080ti_phase1.json
{
  "gpu_model": "NVIDIA GeForce RTX 2080 Ti",
  "phase": "Phase 1 - GLV + Batch Inverse",
  "min_keys_per_sec": 2180000000,
  "max_variance_pct": 5.0,
  "optimizations": [
    "GLV Endomorphism (1.5-1.8× speedup)",
    "Montgomery Batch Inverse (1.3-1.5× speedup)"
  ],
  "expected_improvement": "1.70×",
  "baseline_comparison": {
    "previous": 1280000000,
    "current": 2180000000,
    "improvement_pct": 70.3
  },
  "gpu_metrics": {
    "gpu_utilization_target": 90,
    "memory_bandwidth_utilization": 70,
    "l1_cache_hit_rate": 80,
    "occupancy": 60,
    "register_pressure": 110
  },
  "digest": {
    "algorithm": "SHA-256",
    "hash": "...",
    "timestamp": "2025-10-12T12:34:56Z"
  }
}

// benchmarks/baselines/rtx2080ti_phase2.json
{
  "gpu_model": "NVIDIA GeForce RTX 2080 Ti",
  "phase": "Phase 2 - SoA + Warp Optimization",
  "min_keys_per_sec": 3680000000,
  "max_variance_pct": 5.0,
  "optimizations": [
    "Structure-of-Arrays layout (2.5× memory bandwidth)",
    "Warp shuffle primitives (1.2-1.5× speedup)",
    "Shared memory bank conflict elimination"
  ],
  "expected_improvement": "1.69×",
  "baseline_comparison": {
    "previous": 2180000000,
    "current": 3680000000,
    "improvement_pct": 68.8
  },
  "gpu_metrics": {
    "gpu_utilization_target": 93,
    "memory_bandwidth_utilization": 85,
    "l1_cache_hit_rate": 90,
    "occupancy": 70,
    "register_pressure": 105
  },
  "digest": {
    "algorithm": "SHA-256",
    "hash": "...",
    "timestamp": "2025-10-12T12:34:56Z"
  }
}

// benchmarks/baselines/rtx2080ti_phase3.json
{
  "gpu_model": "NVIDIA GeForce RTX 2080 Ti",
  "phase": "Phase 3 - Async Pipeline",
  "min_keys_per_sec": 4970000000,
  "max_variance_pct": 5.0,
  "optimizations": [
    "CPU-GPU async pipeline (1.3-1.5× speedup)",
    "CUDA Graphs (1.1-1.2× speedup)",
    "Multi-stream execution"
  ],
  "expected_improvement": "1.35×",
  "baseline_comparison": {
    "previous": 3680000000,
    "current": 4970000000,
    "improvement_pct": 35.1
  },
  "gpu_metrics": {
    "gpu_utilization_target": 95,
    "memory_bandwidth_utilization": 88,
    "l1_cache_hit_rate": 92,
    "occupancy": 75,
    "register_pressure": 99
  },
  "digest": {
    "algorithm": "SHA-256",
    "hash": "...",
    "timestamp": "2025-10-12T12:34:56Z"
  }
}

// benchmarks/baselines/rtx2080ti_phase4_final.json
{
  "gpu_model": "NVIDIA GeForce RTX 2080 Ti",
  "phase": "Phase 4 - Final Tuning",
  "min_keys_per_sec": 5720000000,
  "max_variance_pct": 5.0,
  "optimizations": [
    "Nsight Compute profiling optimization",
    "Kernel parameter auto-tuning",
    "Memory access pattern fine-tuning"
  ],
  "expected_improvement": "1.15×",
  "baseline_comparison": {
    "previous": 4970000000,
    "current": 5720000000,
    "improvement_pct": 15.1
  },
  "total_improvement": {
    "original_baseline": 1280000000,
    "final_throughput": 5720000000,
    "total_speedup": "4.47×"
  },
  "gpu_metrics": {
    "gpu_utilization_target": 95,
    "memory_bandwidth_utilization": 90,
    "l1_cache_hit_rate": 92,
    "l2_cache_hit_rate": 85,
    "occupancy": 75,
    "register_pressure": 99
  },
  "digest": {
    "algorithm": "SHA-256",
    "hash": "...",
    "timestamp": "2025-10-12T12:34:56Z"
  }
}
```

---

**5. MANDATORY-DIGEST 原则**
- ✅ 所有artifact必须包含SHA-256摘要
- ✅ 检查点、配置、基线文件全部加密
- ✅ 防篡改验证

**实施方案**:
```cpp
// Storage/checkpoint_manager.cpp
class CheckpointManager {
public:
    void Save(const Checkpoint& checkpoint, const std::string& path) {
        // 1. 序列化检查点
        std::string json = checkpoint.ToJSON();

        // 2. 计算SHA-256摘要
        uint8_t digest[32];
        SHA256((uint8_t*)json.data(), json.size(), digest);

        // 3. 添加摘要到JSON
        nlohmann::json j = nlohmann::json::parse(json);
        j["digest"] = {
            {"algorithm", "SHA-256"},
            {"hash", BytesToHex(digest, 32)},
            {"timestamp", GetCurrentTimestamp()}
        };

        // 4. 加密保存
        EncryptAndSave(j.dump(), path);
    }

    Checkpoint Load(const std::string& path) {
        // 1. 解密加载
        std::string json = DecryptAndLoad(path);

        // 2. 验证摘要
        nlohmann::json j = nlohmann::json::parse(json);
        std::string expected_hash = j["digest"]["hash"];

        // 3. 重新计算摘要
        j.erase("digest");
        std::string payload = j.dump();
        uint8_t digest[32];
        SHA256((uint8_t*)payload.data(), payload.size(), digest);
        std::string actual_hash = BytesToHex(digest, 32);

        // 4. 验证一致性
        if (expected_hash != actual_hash) {
            throw std::runtime_error("Checkpoint digest mismatch!");
        }

        return Checkpoint::FromJSON(payload);
    }
};
```

---

**6. 操作员审计追踪**
- ✅ 所有运行必须记录operator-id和purpose
- ✅ WORM审计日志（只写不改）
- ✅ 完整性检查和摘要比对

**实施方案**:
```cpp
// Interface/cli_interface.cpp
int main(int argc, char* argv[]) {
    // 强制CLI参数
    if (!HasArg("--operator-id") || !HasArg("--operator-purpose")) {
        std::cerr << "Error: --operator-id and --operator-purpose are required!" << std::endl;
        return 1;
    }

    std::string operator_id = GetArg("--operator-id");
    std::string operator_purpose = GetArg("--operator-purpose");

    // 记录审计日志（WORM格式）
    AuditLogger logger("audit/audit.jsonl");
    logger.Log({
        {"timestamp", GetCurrentTimestamp()},
        {"hostname", GetHostname()},
        {"operator_id", operator_id},
        {"operator_purpose", operator_purpose},
        {"keyspace", GetArg("--keyspace")},
        {"target_address", GetArg("--target-address")},
        {"digest", ComputeDigest()}
    });

    // 执行求解
    SuperBitcoinPuzzleSolver solver = SuperBitcoinPuzzleSolver::Create(config);
    SearchResult result = solver.Solve();

    return 0;
}
```

---

### 工程常量与质量门禁

**性能指标**:
```yaml
ProjectConstants:
  # 性能指标（融合最新技术后的目标）
  TargetP99Latency: 50           # ms (优化后降低50%)

  # 分阶段性能目标（RTX 2080 Ti）
  TargetThroughput:
    Phase1_Baseline: 1280        # Mkeys/sec (当前基线)
    Phase2_GLV_BatchInv: 2180    # Mkeys/sec (+70%, GLV+批量逆元)
    Phase3_SoA_Warp: 3680        # Mkeys/sec (+69%, SoA+Warp优化)
    Phase4_Async_Pipeline: 4970  # Mkeys/sec (+35%, 异步流水线)
    Phase5_Final_Tuning: 5720    # Mkeys/sec (+15%, 最终调优)

  # 多GPU性能目标
  MultiGPU_Throughput:
    RTX_2080Ti_x1: 5720          # Mkeys/sec
    RTX_2080Ti_x2: 10868         # Mkeys/sec (95%扩展性)
    RTX_2080Ti_x4: 21736         # Mkeys/sec (95%扩展性)
    RTX_2080Ti_x8: 43472         # Mkeys/sec (95%扩展性)

  # 不同GPU架构目标
  GPU_Specific_Targets:
    RTX_2080Ti_Turing: 5720      # Mkeys/sec (基线)
    RTX_3090_Ampere: 11200       # Mkeys/sec (1.96×)
    H20_Hopper: 17900            # Mkeys/sec (3.13×)
    A100_Ampere: 22880           # Mkeys/sec (4.0×)

  # GPU资源利用率目标
  MaxMemoryUsage: 2048           # MB
  MaxCPUUsage: 80               # %
  GPUUtilizationTarget: 95      # % (优化后提升至95%)
  MemoryBandwidthUtilization: 90 # % (SoA布局优化)
  L1CacheHitRate: 92            # % (共享内存优化)
  L2CacheHitRate: 85            # %
  RegisterPressure: 99          # regs/thread (最大128)
  Occupancy: 75                 # % (优化后提升至75%)

  # 代码质量门禁
  TargetTestCoverage: 90        # %
  MaxFunctionLength: 30         # lines
  MaxCyclomaticComplexity: 8
  MaxCompilerWarnings: 0
  CodeSmellThreshold: 0

  # 安全性要求（密码学项目特殊要求）
  SecurityLevel: "CRYPTO_HIGHEST"
  MaxVulnerabilitySeverity: "NONE"
  RequiredSanitizers: ["address", "undefined", "thread", "memory"]

  # 可维护性标准
  DocumentationCoverage: 95     # %
  MaxTechnicalDebtRatio: 3      # %
  CommentDensity: 20            # %

  # 可靠性指标
  TargetUptime: 99.99           # %
  MaxErrorRate: 0.01            # %
  MeanTimeToRecovery: 60        # seconds

  # 密码学特定要求
  KeySearchAccuracy: 100        # % (必须100%准确)
  RandomnessQuality: "CRYPTO_SECURE"
  SideChannelProtection: true

  # 性能提升路线图
  PerformanceRoadmap:
    Current_Baseline: 1.28       # Gkeys/sec
    Phase1_Target: 2.18          # Gkeys/sec (+70%)
    Phase2_Target: 3.68          # Gkeys/sec (+69%)
    Phase3_Target: 4.97          # Gkeys/sec (+35%)
    Phase4_Target: 5.72          # Gkeys/sec (+15%)
    Total_Improvement: 4.47x     # 总提升倍数
```

---

### VerificationGauntlet（6阶段验证）

**Stage 1: 编译与静态检查**
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 静态分析
clang-tidy src/**/*.cpp -- -std=c++17
cppcheck --enable=all --std=c++17 src/
```

**Stage 2: 单元与集成测试**
```bash
cd build
ctest -R unit_tests --output-on-failure
ctest -R integration_tests --output-on-failure
```

**Stage 3: GPU吞吐基准**
```bash
scripts/run_performance_benchmark.sh rtx2080ti
# 验证：吞吐量 ≥ 1000M keys/sec
```

**Stage 4: GPU/CPU Parity验证**
```bash
scripts/run_parity_validation.sh
# 验证：GPU结果与bitcoin-core/secp256k1一致（误差<1e-10）
```

**Stage 5: Replay Consistency**
```bash
scripts/replay/verify-replay.sh <manifest> <telemetry-jsonl>
# 验证：重放结果完全一致
```

**Stage 6: 完整性校验与报告**
```bash
scripts/digest/check-artifact-digests.sh
scripts/generate-report.sh
# 验证：所有artifact摘要正确
```

---

## 📊 项目架构设计

### 整体架构（分层设计）

```
SuperBitcoinPuzzleSolver/
├── Core Layer (核心层)
│   ├── ECC Engine (椭圆曲线引擎)
│   │   ├── VanitySearch GLV Endomorphism (提取)
│   │   ├── BitCrack Batch Inverse (提取)
│   │   └── Unified secp256k1 Interface
│   ├── Hash Engine (哈希引擎)
│   │   ├── BitCrack SHA256/RIPEMD160 (提取)
│   │   └── CUDA优化哈希实现
│   └── Memory Manager (内存管理)
│       ├── SoA布局 (新设计)
│       └── GPU内存池
│
├── Algorithm Layer (算法层)
│   ├── Brute Force Mode (无公钥模式)
│   │   ├── VanitySearch范围扫描 (提取)
│   │   ├── BitCrack批量处理 (提取)
│   │   └── 动态负载均衡 (新设计)
│   ├── BSGS Mode (有公钥模式)
│   │   ├── Keyhunt BSGS算法 (提取)
│   │   ├── GPU Bloom Filter (提取)
│   │   └── 内存优化策略 (新设计)
│   └── Pollard Kangaroo Mode (有公钥模式)
│       ├── Keyhunt Kangaroo算法 (提取)
│       ├── 碰撞检测 (提取)
│       └── 分布式协同 (新设计)
│
├── GPU Layer (GPU执行层)
│   ├── Kernel Manager (内核管理)
│   │   ├── VanitySearch PTX优化 (提取)
│   │   ├── Cooperative Groups (新增)
│   │   └── 动态内核选择
│   ├── Multi-GPU Scheduler (多GPU调度)
│   │   ├── NCCL通信 (新增)
│   │   ├── 负载均衡 (新设计)
│   │   └── 容错恢复
│   └── Performance Monitor (性能监控)
│       ├── 实时吞吐量统计
│       ├── GPU利用率监控
│       └── 自动调优
│
├── Storage Layer (存储层)
│   ├── Checkpoint Manager (检查点管理)
│   │   ├── BitCrack检查点格式 (提取)
│   │   ├── 增量保存 (新设计)
│   │   └── 加密存储
│   ├── Result Manager (结果管理)
│   │   ├── 结果验证 (CPU验证)
│   │   ├── 结果去重
│   │   └── 结果导出
│   └── Telemetry Logger (遥测日志)
│       ├── JSONL格式 (保持)
│       └── SHA-256摘要
│
└── Interface Layer (接口层)
    ├── CLI Interface (命令行接口)
    │   ├── 统一参数设计
    │   ├── 交互式模式
    │   └── 批处理模式
    ├── Config Manager (配置管理)
    │   ├── YAML配置文件
    │   ├── 配置验证
    │   └── 配置热更新
    └── API Interface (API接口)
        ├── RESTful API (可选)
        ├── gRPC API (可选)
        └── Python绑定 (可选)
```

---

## 🔧 各库功能提取清单

### 1. VanitySearch提取清单

**提取文件**:
```
VanitySearch/
├── SECP256k1.cpp/h          → Core/ECC/glv_endomorphism.cpp/h
│   ├── SplitScalar()        → GLV分解算法
│   ├── ComputePublicKey()   → 公钥计算
│   └── 常量定义 (BETA, LAMBDA)
├── IntGroup.cpp/h           → Core/ECC/batch_inverse.cpp/h
│   ├── ModInv()             → Montgomery批量逆元
│   └── Set()                → 批量设置
├── GPU/GPUEngine.cu/h       → GPU/Kernels/ecc_kernel.cu/h
│   ├── FindKeyGPU()         → GPU扫描内核
│   ├── CheckHash()          → 哈希检查
│   └── PTX优化代码
└── Timer.cpp/h              → Utils/timer.cpp/h
    └── 性能计时器
```

**关键算法**:
1. **GLV Endomorphism**: 标量乘法加速1.5-1.8×
2. **批量逆元**: Montgomery算法，减少逆元计算次数
3. **PTX内联汇编**: 256位大整数运算优化
4. **范围扫描**: 高效的密钥范围遍历

**提取难度**: ⭐⭐⭐⭐ (4/5)  
**提取工作量**: 80小时  

---

### 2. BitCrack提取清单

**提取文件**:
```
BitCrack/
├── cudaMath/secp256k1.cuh   → Core/ECC/secp256k1_math.cuh
│   ├── addModP()            → 模加法
│   ├── mulModP()            → 模乘法
│   ├── invModP()            → 模逆元
│   └── doBatchInverse()     → 批量逆元
├── CudaKeySearchDevice/
│   ├── CudaDeviceKeys.cu    → GPU/Kernels/key_generator.cu
│   │   ├── generateBatchKeys() → 批量密钥生成
│   │   └── incrementKeys()     → 密钥递增
│   ├── CudaAtomicList.cu    → GPU/Utils/atomic_list.cu
│   │   └── atomicListAdd()  → 原子操作结果列表
│   └── CudaHashLookup.cu    → GPU/Kernels/hash_lookup.cu
│       └── checkHash()      → 哈希查找
├── hash/sha256.cu           → Core/Hash/sha256.cu
│   └── sha256()             → SHA256 CUDA实现
├── hash/ripemd160.cu        → Core/Hash/ripemd160.cu
│   └── ripemd160()          → RIPEMD160 CUDA实现
└── DeviceContextShared.h    → GPU/device_context.h
    └── GPU上下文管理
```

**关键算法**:
1. **批量密钥生成**: 高效的GPU密钥生成
2. **SHA256/RIPEMD160**: CUDA优化的哈希实现
3. **原子操作结果列表**: 线程安全的结果收集
4. **多GPU支持**: 多GPU协同框架

**提取难度**: ⭐⭐⭐ (3/5)  
**提取工作量**: 60小时  

---

### 3. Keyhunt提取清单

**提取文件**:
```
Keyhunt/
├── keyhunt.c                → Algorithm/BSGS/bsgs_cpu.cpp
│   ├── bsgs_search()        → BSGS算法主逻辑
│   ├── bsgs_init()          → BSGS初始化
│   └── bsgs_free()          → BSGS清理
├── sha256.cu                → Core/Hash/sha256_keyhunt.cu
│   └── sha256_gpu()         → SHA256 GPU实现
├── rmd160.cu                → Core/Hash/rmd160_keyhunt.cu
│   └── rmd160_gpu()         → RIPEMD160 GPU实现
├── bloom.c                  → Algorithm/BSGS/bloom_filter.cpp
│   ├── bloom_init()         → Bloom Filter初始化
│   ├── bloom_add()          → 添加元素
│   └── bloom_check()        → 检查元素
└── util.c                   → Utils/util.cpp
    ├── hexs2bin()           → 十六进制转换
    └── point_to_cpupoint()  → 点格式转换
```

**关键算法**:
1. **BSGS算法**: Baby Step Giant Step，适用于有公钥场景
2. **Bloom Filter**: 内存优化的哈希表
3. **Pollard Kangaroo**: 碰撞检测算法
4. **内存管理**: 大规模哈希表管理

**提取难度**: ⭐⭐⭐⭐ (4/5)  
**提取工作量**: 100小时  

---

## 🏗️ 融合架构设计

### 架构模式选择

**采用分层架构 + 策略模式 + 适配器模式**

```cpp
// 1. 核心接口定义
namespace sbps {

// ECC引擎接口
class IECCEngine {
public:
    virtual ~IECCEngine() = default;
    virtual void ScalarMul(const Scalar& k, const Point& G, Point& result) = 0;
    virtual void BatchInverse(Scalar* scalars, size_t count) = 0;
    virtual void GLVDecompose(const Scalar& k, Scalar& k1, Scalar& k2) = 0;
};

// 哈希引擎接口
class IHashEngine {
public:
    virtual ~IHashEngine() = default;
    virtual void SHA256(const uint8_t* input, size_t len, uint8_t* output) = 0;
    virtual void RIPEMD160(const uint8_t* input, size_t len, uint8_t* output) = 0;
    virtual void Hash160(const Point& pubkey, uint8_t* hash160) = 0;
};

// 搜索策略接口
class ISearchStrategy {
public:
    virtual ~ISearchStrategy() = default;
    virtual void Initialize(const SearchConfig& config) = 0;
    virtual SearchResult Execute() = 0;
    virtual void SaveCheckpoint(const std::string& path) = 0;
    virtual void LoadCheckpoint(const std::string& path) = 0;
};

// 2. 具体实现（适配器模式）

// VanitySearch GLV适配器
class VanitySearchGLVAdapter : public IECCEngine {
private:
    // 包装VanitySearch的SECP256k1类
    VanitySearch::Secp256K1 secp_;
    
public:
    void ScalarMul(const Scalar& k, const Point& G, Point& result) override {
        // 使用GLV endomorphism加速
        Scalar k1, k2;
        GLVDecompose(k, k1, k2);
        
        Point P1 = secp_.ComputePublicKey(k1);
        Point P2 = secp_.ComputePublicKey(k2);
        P2 = secp_.ApplyEndomorphism(P2);  // 应用phi
        
        result = secp_.AddPoints(P1, P2);
    }
    
    void BatchInverse(Scalar* scalars, size_t count) override {
        // 使用VanitySearch的IntGroup批量逆元
        VanitySearch::IntGroup group(count);
        for (size_t i = 0; i < count; i++) {
            group.Set(i, scalars[i]);
        }
        group.ModInv();
        for (size_t i = 0; i < count; i++) {
            scalars[i] = group.Get(i);
        }
    }
    
    void GLVDecompose(const Scalar& k, Scalar& k1, Scalar& k2) override {
        // 直接调用VanitySearch的SplitScalar
        secp_.SplitScalar(&k, &k1, &k2);
    }
};

// BitCrack哈希适配器
class BitCrackHashAdapter : public IHashEngine {
public:
    void SHA256(const uint8_t* input, size_t len, uint8_t* output) override {
        // 调用BitCrack的CUDA SHA256
        BitCrack::sha256_cuda(input, len, output);
    }
    
    void RIPEMD160(const uint8_t* input, size_t len, uint8_t* output) override {
        // 调用BitCrack的CUDA RIPEMD160
        BitCrack::ripemd160_cuda(input, len, output);
    }
    
    void Hash160(const Point& pubkey, uint8_t* hash160) override {
        uint8_t sha256_hash[32];
        SHA256(pubkey.data(), pubkey.size(), sha256_hash);
        RIPEMD160(sha256_hash, 32, hash160);
    }
};

// 3. 搜索策略实现（策略模式）

// 暴力搜索策略（无公钥）
class BruteForceStrategy : public ISearchStrategy {
private:
    std::unique_ptr<IECCEngine> ecc_engine_;
    std::unique_ptr<IHashEngine> hash_engine_;
    std::unique_ptr<MultiGPUScheduler> gpu_scheduler_;
    
public:
    BruteForceStrategy(
        std::unique_ptr<IECCEngine> ecc,
        std::unique_ptr<IHashEngine> hash
    ) : ecc_engine_(std::move(ecc)), hash_engine_(std::move(hash)) {
        // 使用VanitySearch的范围扫描 + BitCrack的批量处理
    }
    
    SearchResult Execute() override {
        // 1. 使用VanitySearch的GLV加速标量乘法
        // 2. 使用BitCrack的批量密钥生成
        // 3. 使用BitCrack的哈希计算
        // 4. 使用多GPU并行处理
    }
};

// BSGS策略（有公钥）
class BSGSStrategy : public ISearchStrategy {
private:
    std::unique_ptr<IECCEngine> ecc_engine_;
    std::unique_ptr<BloomFilter> bloom_filter_;
    
public:
    BSGSStrategy(std::unique_ptr<IECCEngine> ecc)
        : ecc_engine_(std::move(ecc)) {
        // 使用Keyhunt的BSGS算法 + VanitySearch的GLV优化
    }
    
    SearchResult Execute() override {
        // 1. Baby Step: 使用VanitySearch GLV加速
        // 2. Giant Step: 使用Keyhunt的Bloom Filter
        // 3. 碰撞检测: 使用Keyhunt的碰撞检测逻辑
    }
};

// Pollard Kangaroo策略（有公钥）
class PollardKangarooStrategy : public ISearchStrategy {
private:
    std::unique_ptr<IECCEngine> ecc_engine_;
    std::unique_ptr<DistinguishedPointTable> dp_table_;
    
public:
    PollardKangarooStrategy(std::unique_ptr<IECCEngine> ecc)
        : ecc_engine_(std::move(ecc)) {
        // 使用Keyhunt的Kangaroo算法 + VanitySearch的GLV优化
    }
    
    SearchResult Execute() override {
        // 1. Tame/Wild Kangaroo: 使用VanitySearch GLV加速
        // 2. Distinguished Points: 使用Keyhunt的DP表
        // 3. 碰撞检测: 使用Keyhunt的碰撞检测逻辑
    }
};

// 4. 统一的求解器接口
class SuperBitcoinPuzzleSolver {
private:
    std::unique_ptr<ISearchStrategy> strategy_;
    std::unique_ptr<CheckpointManager> checkpoint_mgr_;
    std::unique_ptr<TelemetryLogger> telemetry_;
    
public:
    // 工厂方法：根据场景选择策略
    static std::unique_ptr<SuperBitcoinPuzzleSolver> Create(
        const PuzzleConfig& config
    ) {
        std::unique_ptr<ISearchStrategy> strategy;
        
        if (config.has_public_key) {
            if (config.use_bsgs) {
                strategy = std::make_unique<BSGSStrategy>(...);
            } else {
                strategy = std::make_unique<PollardKangarooStrategy>(...);
            }
        } else {
            strategy = std::make_unique<BruteForceStrategy>(...);
        }
        
        return std::make_unique<SuperBitcoinPuzzleSolver>(std::move(strategy));
    }
    
    // 统一的求解接口
    SearchResult Solve() {
        // 1. 加载检查点（如果存在）
        if (checkpoint_mgr_->Exists()) {
            strategy_->LoadCheckpoint(checkpoint_mgr_->GetPath());
        }
        
        // 2. 执行搜索
        SearchResult result = strategy_->Execute();
        
        // 3. 保存检查点
        strategy_->SaveCheckpoint(checkpoint_mgr_->GetPath());
        
        // 4. 记录遥测
        telemetry_->Log(result);
        
        return result;
    }
};

} // namespace sbps
```

---

## 📋 详细执行计划

### 阶段1: 基础设施搭建（2周）

**任务1.1: 项目目录结构（2天）**

**目录结构设计**:
```
SuperBitcoinPuzzleSolver/
├── .github/
│   ├── workflows/
│   │   ├── ci-build-test.yml          # CI构建和测试
│   │   ├── ci-performance-gate.yml    # 性能门禁
│   │   ├── ci-security-scan.yml       # 安全扫描
│   │   └── ci-determinism-check.yml   # 确定性检查
│   └── ISSUE_TEMPLATE/
│       ├── bug_report.md
│       └── feature_request.md
├── src/
│   ├── Core/                          # 核心层
│   │   ├── ECC/                       # ECC引擎
│   │   │   ├── glv_endomorphism.cpp/h
│   │   │   ├── batch_inverse.cpp/h
│   │   │   ├── secp256k1_math.cuh
│   │   │   └── unified_ecc_engine.cpp/h
│   │   ├── Hash/                      # 哈希引擎
│   │   │   ├── sha256.cu/h
│   │   │   ├── ripemd160.cu/h
│   │   │   └── unified_hash_engine.cpp/h
│   │   └── Memory/                    # 内存管理
│   │       ├── soa_layout.h
│   │       └── gpu_memory_pool.cpp/h
│   ├── Algorithm/                     # 算法层
│   │   ├── BruteForce/
│   │   ├── BSGS/
│   │   └── Kangaroo/
│   ├── GPU/                           # GPU执行层
│   │   ├── Kernels/
│   │   ├── multi_gpu_scheduler.cpp/h
│   │   └── performance_monitor.cpp/h
│   ├── Storage/                       # 存储层
│   │   ├── checkpoint_manager.cpp/h
│   │   ├── result_manager.cpp/h
│   │   └── telemetry_logger.cpp/h
│   └── Interface/                     # 接口层
│       ├── cli_interface.cpp
│       └── config_manager.cpp/h
├── tests/
│   ├── unit/                          # 单元测试
│   │   ├── core/
│   │   ├── algorithm/
│   │   ├── gpu/
│   │   └── storage/
│   ├── integration/                   # 集成测试
│   ├── validation/                    # 验证测试
│   │   ├── parity_checker.cpp
│   │   └── deterministic_replay.cpp
│   ├── performance/                   # 性能测试
│   │   └── benchmark_runner.cpp
│   └── fuzz/                          # 模糊测试
├── benchmarks/
│   ├── baselines/                     # 性能基线
│   │   ├── rtx2080ti.json
│   │   ├── rtx3090.json
│   │   └── a100.json
│   └── telemetry/                     # 遥测数据
├── config/
│   ├── puzzle71.yaml                  # 主配置
│   ├── deterministic.yaml             # 确定性配置
│   └── security.yaml                  # 安全配置
├── scripts/
│   ├── build/
│   │   ├── build.sh
│   │   └── clean.sh
│   ├── test/
│   │   ├── run_unit_tests.sh
│   │   ├── run_integration_tests.sh
│   │   └── run_performance_tests.sh
│   ├── ci/
│   │   ├── performance_gate.sh
│   │   ├── security_scan.sh
│   │   └── determinism_check.sh
│   ├── replay/
│   │   └── verify-replay.sh
│   └── digest/
│       └── check-artifact-digests.sh
├── docs/
│   ├── architecture/                  # 架构文档
│   ├── api/                           # API文档
│   ├── validation/                    # 验证文档
│   └── plans/                         # 计划文档
├── audit/                             # 审计日志（WORM）
├── checkpoints/                       # 检查点
├── results/                           # 结果
├── CMakeLists.txt
├── README.md
├── CONTRIBUTING.md
├── LICENSE
└── .gitignore
```

**铁笼协议要求**:
- ✅ 分离测试代码（tests/）
- ✅ 独立基准测试（benchmarks/）
- ✅ 审计日志目录（audit/）
- ✅ 检查点目录（checkpoints/）
- ✅ CI/CD配置（.github/workflows/）

**交付物**:
- 完整的目录结构
- .gitignore配置
- README.md框架

**工作量**: 16小时

---

**任务1.2: CMake构建系统（3天）**

**CMakeLists.txt设计**:
```cmake
cmake_minimum_required(VERSION 3.22)
project(SuperBitcoinPuzzleSolver LANGUAGES CXX CUDA)

# 铁笼协议：C++17标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# 铁笼协议：CUDA架构支持
set(CMAKE_CUDA_ARCHITECTURES "75;86;89;90")

# 铁笼协议：编译器警告（零容忍）
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Werror -pedantic)
endif()

# 铁笼协议：优化标志
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CUDA_FLAGS_RELEASE "-O3 --use_fast_math -DNDEBUG")

# 铁笼协议：Sanitizers（调试模式）
if(CMAKE_BUILD_TYPE MATCHES "Debug")
    add_compile_options(-fsanitize=address,undefined,thread,memory)
    add_link_options(-fsanitize=address,undefined,thread,memory)
endif()

# 依赖管理（铁笼协议：SHA256校验）
include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
)

FetchContent_MakeAvailable(nlohmann_json googletest)

# 引用源（铁笼协议：不造轮子）
add_subdirectory(external/VanitySearch)
add_subdirectory(external/BitCrack)
add_subdirectory(external/Keyhunt)

# 核心库
add_library(sbps_core
    src/Core/ECC/glv_endomorphism.cpp
    src/Core/ECC/batch_inverse.cpp
    src/Core/ECC/unified_ecc_engine.cpp
    src/Core/Hash/sha256.cu
    src/Core/Hash/ripemd160.cu
    src/Core/Hash/unified_hash_engine.cpp
    src/Core/Memory/gpu_memory_pool.cpp
)

target_link_libraries(sbps_core
    PRIVATE
        VanitySearch::secp256k1
        BitCrack::cudaMath
        Keyhunt::util
        CUDA::cudart
)

# 算法库
add_library(sbps_algorithm
    src/Algorithm/BruteForce/brute_force_strategy.cpp
    src/Algorithm/BSGS/bsgs_strategy.cpp
    src/Algorithm/Kangaroo/kangaroo_strategy.cpp
)

# GPU库
add_library(sbps_gpu
    src/GPU/Kernels/ecc_kernel.cu
    src/GPU/Kernels/hash_kernel.cu
    src/GPU/multi_gpu_scheduler.cpp
    src/GPU/performance_monitor.cpp
)

# 主程序
add_executable(sbps
    src/Interface/cli_interface.cpp
    src/Interface/config_manager.cpp
)

target_link_libraries(sbps
    PRIVATE
        sbps_core
        sbps_algorithm
        sbps_gpu
        nlohmann_json::nlohmann_json
)

# 测试（铁笼协议：测试优先）
enable_testing()

add_executable(sbps_unit_tests
    tests/unit/core/test_glv_endomorphism.cu
    tests/unit/core/test_batch_inverse.cpp
    tests/unit/algorithm/test_brute_force.cpp
    tests/unit/algorithm/test_bsgs.cpp
)

target_link_libraries(sbps_unit_tests
    PRIVATE
        sbps_core
        sbps_algorithm
        GTest::gtest_main
)

add_test(NAME UnitTests COMMAND sbps_unit_tests)

# 性能测试（铁笼协议：性能门禁）
add_executable(sbps_performance_tests
    tests/performance/benchmark_runner.cpp
)

target_link_libraries(sbps_performance_tests
    PRIVATE
        sbps_core
        sbps_gpu
)

add_test(NAME PerformanceTests COMMAND sbps_performance_tests)

# 验证测试（铁笼协议：CPU/GPU一致性）
add_executable(sbps_validation_tests
    tests/validation/parity_checker.cpp
    tests/validation/deterministic_replay.cpp
)

target_link_libraries(sbps_validation_tests
    PRIVATE
        sbps_core
        sbps_gpu
        secp256k1  # bitcoin-core/secp256k1
)

add_test(NAME ValidationTests COMMAND sbps_validation_tests)

# 安装
install(TARGETS sbps DESTINATION bin)
install(DIRECTORY config/ DESTINATION etc/sbps)
install(DIRECTORY docs/ DESTINATION share/doc/sbps)
```

**铁笼协议要求**:
- ✅ C++17标准
- ✅ CUDA架构75/86/89/90
- ✅ 零编译警告
- ✅ SHA256校验依赖
- ✅ Sanitizers支持
- ✅ 测试优先

**交付物**:
- CMakeLists.txt
- external/目录（VanitySearch/BitCrack/Keyhunt子模块）
- 可编译的最小示例

**工作量**: 24小时

---

**任务1.3: CI/CD流水线（4天）**

**GitHub Actions配置**:

**1. CI构建和测试（.github/workflows/ci-build-test.yml）**:
```yaml
name: CI Build and Test

on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main ]

jobs:
  build-and-test:
    runs-on: ubuntu-latest

    steps:
    - uses: actions/checkout@v3
      with:
        submodules: recursive

    # 铁笼协议：确定性构建
    - name: Setup deterministic environment
      run: |
        export SOURCE_DATE_EPOCH=1609459200
        export TZ=UTC

    - name: Install dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y \
          cmake \
          ninja-build \
          libsecp256k1-dev \
          clang-tidy \
          cppcheck

    # 铁笼协议：静态分析
    - name: Run static analysis
      run: |
        clang-tidy src/**/*.cpp -- -std=c++17
        cppcheck --enable=all --error-exitcode=1 src/

    - name: Build
      run: |
        mkdir build && cd build
        cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release
        ninja -j$(nproc)

    # 铁笼协议：单元测试
    - name: Run unit tests
      run: |
        cd build
        ctest -R unit_tests --output-on-failure

    # 铁笼协议：集成测试
    - name: Run integration tests
      run: |
        cd build
        ctest -R integration_tests --output-on-failure

    # 铁笼协议：验证测试
    - name: Run validation tests
      run: |
        cd build
        ctest -R validation_tests --output-on-failure

    # 铁笼协议：测试覆盖率
    - name: Generate coverage report
      run: |
        cd build
        ninja coverage
        bash <(curl -s https://codecov.io/bash)
```

**2. 性能门禁（.github/workflows/ci-performance-gate.yml）**:
```yaml
name: Performance Gate

on:
  push:
    branches: [ main ]
  pull_request:
    branches: [ main ]

jobs:
  performance-gate:
    runs-on: [self-hosted, gpu]  # 需要GPU的自托管runner

    steps:
    - uses: actions/checkout@v3

    - name: Build
      run: |
        mkdir build && cd build
        cmake .. -DCMAKE_BUILD_TYPE=Release
        make -j$(nproc)

    # 铁笼协议：性能基准测试
    - name: Run performance benchmark
      run: |
        scripts/run_performance_benchmark.sh rtx2080ti

    # 铁笼协议：性能门禁检查
    - name: Check performance gate
      run: |
        scripts/ci/performance_gate.sh rtx2080ti

    # 铁笼协议：上传遥测数据
    - name: Upload telemetry
      uses: actions/upload-artifact@v3
      with:
        name: telemetry
        path: benchmarks/telemetry/
```

**3. 安全扫描（.github/workflows/ci-security-scan.yml）**:
```yaml
name: Security Scan

on:
  push:
    branches: [ main ]
  schedule:
    - cron: '0 0 * * 0'  # 每周日运行

jobs:
  security-scan:
    runs-on: ubuntu-latest

    steps:
    - uses: actions/checkout@v3

    # 铁笼协议：依赖漏洞扫描
    - name: Run dependency check
      run: |
        scripts/ci/security_scan.sh

    # 铁笼协议：代码安全扫描
    - name: Run CodeQL analysis
      uses: github/codeql-action/analyze@v2

    # 铁笼协议：密钥泄露检测
    - name: Run secret scanning
      uses: trufflesecurity/trufflehog@main
      with:
        path: ./
```

**4. 确定性检查（.github/workflows/ci-determinism-check.yml）**:
```yaml
name: Determinism Check

on:
  push:
    branches: [ main ]

jobs:
  determinism-check:
    runs-on: [self-hosted, gpu]

    steps:
    - uses: actions/checkout@v3

    - name: Build
      run: |
        mkdir build && cd build
        cmake .. -DCMAKE_BUILD_TYPE=Release
        make -j$(nproc)

    # 铁笼协议：确定性重放验证
    - name: Run deterministic replay
      run: |
        # 第一次运行
        ./build/sbps \
          --keyspace 0x1:0x1000 \
          --target-address 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU \
          --operator-id ci \
          --operator-purpose determinism-check \
          --save-manifest checkpoints/run1_manifest.json

        # 第二次运行（使用相同配置）
        ./build/sbps \
          --replay-manifest checkpoints/run1_manifest.json \
          --operator-id ci \
          --operator-purpose determinism-check \
          --save-manifest checkpoints/run2_manifest.json

        # 验证两次运行结果一致
        scripts/replay/verify-replay.sh \
          checkpoints/run1_manifest.json \
          checkpoints/run2_manifest.json
```

**铁笼协议要求**:
- ✅ 静态分析（clang-tidy/cppcheck）
- ✅ 单元/集成/验证测试
- ✅ 测试覆盖率≥90%
- ✅ 性能门禁（RTX 2080 Ti ≥ 1000M keys/sec）
- ✅ 安全扫描（依赖/代码/密钥）
- ✅ 确定性重放验证

**交付物**:
- 4个GitHub Actions工作流
- CI/CD脚本（scripts/ci/）
- 自托管GPU runner配置文档

**工作量**: 32小时

---

**任务1.4: 项目文档框架（3天）**

**文档清单**:
1. **README.md**: 项目介绍、快速开始、构建指南
2. **CONTRIBUTING.md**: 贡献指南、代码规范、提交流程
3. **docs/architecture/**: 架构设计文档
4. **docs/api/**: API文档
5. **docs/validation/**: 验证文档
6. **docs/plans/**: 计划文档

**铁笼协议要求**:
- ✅ 文档覆盖率≥95%
- ✅ 中文主体+英文术语
- ✅ 代码注释英文
- ✅ 引用溯源

**交付物**:
- 完整的文档框架
- 文档生成脚本（Doxygen/Sphinx）

**工作量**: 24小时

---

**阶段1总工作量**: 96小时（12个工作日）

---

**任务1.5: 第三方库克隆与集成（5天）**

**铁笼协议要求**: 不造轮子，复用现有库，全部克隆并集成到项目中

**第三方库清单**:

**1. VanitySearch (JeanLucPons/VanitySearch)**
```bash
# 克隆到external/目录
cd external/
git clone https://github.com/JeanLucPons/VanitySearch.git
cd VanitySearch
git checkout <latest-stable-tag>  # 锁定版本

# 记录版本信息
echo "VanitySearch $(git describe --tags)" > ../../docs/reference-sources.md
```

**集成方案**:
```cmake
# external/VanitySearch/CMakeLists.txt (新建)
cmake_minimum_required(VERSION 3.22)
project(VanitySearch LANGUAGES CXX CUDA)

# 提取核心文件
add_library(VanitySearch_secp256k1 STATIC
    SECP256k1.cpp
    SECP256k1.h
    Int.cpp
    Int.h
    IntGroup.cpp
    IntGroup.h
    Point.cpp
    Point.h
)

target_include_directories(VanitySearch_secp256k1 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 导出目标
add_library(VanitySearch::secp256k1 ALIAS VanitySearch_secp256k1)
```

**提取文件清单**:
- `SECP256k1.cpp/h` - GLV endomorphism实现
- `IntGroup.cpp/h` - 批量逆元实现
- `Int.cpp/h` - 256位大整数运算
- `Point.cpp/h` - 椭圆曲线点运算
- `GPU/GPUEngine.cu/h` - GPU kernel实现

**工作量**: 16小时

---

**2. BitCrack (brichard19/BitCrack)**
```bash
# 克隆到external/目录
cd external/
git clone https://github.com/brichard19/BitCrack.git
cd BitCrack
git checkout <latest-stable-tag>  # 锁定版本

# 记录版本信息
echo "BitCrack $(git describe --tags)" >> ../../docs/reference-sources.md
```

**集成方案**:
```cmake
# external/BitCrack/CMakeLists.txt (新建)
cmake_minimum_required(VERSION 3.22)
project(BitCrack LANGUAGES CXX CUDA)

# 提取CUDA数学库
add_library(BitCrack_cudaMath STATIC
    cudaMath/secp256k1.cu
    cudaMath/secp256k1.cuh
    cudaMath/ptx_asm.cuh
)

target_include_directories(BitCrack_cudaMath PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 提取哈希库
add_library(BitCrack_hash STATIC
    hash/sha256.cu
    hash/sha256.cuh
    hash/ripemd160.cu
    hash/ripemd160.cuh
)

# 提取设备管理
add_library(BitCrack_device STATIC
    CudaKeySearchDevice/CudaDeviceKeys.cu
    CudaKeySearchDevice/CudaAtomicList.cu
    CudaKeySearchDevice/CudaHashLookup.cu
)

# 导出目标
add_library(BitCrack::cudaMath ALIAS BitCrack_cudaMath)
add_library(BitCrack::hash ALIAS BitCrack_hash)
add_library(BitCrack::device ALIAS BitCrack_device)
```

**提取文件清单**:
- `cudaMath/secp256k1.cu/cuh` - secp256k1 CUDA实现
- `cudaMath/ptx_asm.cuh` - PTX内联汇编
- `hash/sha256.cu/cuh` - SHA256 CUDA实现
- `hash/ripemd160.cu/cuh` - RIPEMD160 CUDA实现
- `CudaKeySearchDevice/CudaDeviceKeys.cu` - 批量密钥生成
- `CudaKeySearchDevice/CudaAtomicList.cu` - 原子操作结果列表
- `CudaKeySearchDevice/CudaHashLookup.cu` - 哈希查找

**工作量**: 20小时

---

**3. Keyhunt (albertobsd/keyhunt)**
```bash
# 克隆到external/目录
cd external/
git clone https://github.com/albertobsd/keyhunt.git
cd keyhunt
git checkout <latest-stable-tag>  # 锁定版本

# 记录版本信息
echo "Keyhunt $(git describe --tags)" >> ../../docs/reference-sources.md
```

**集成方案**:
```cmake
# external/Keyhunt/CMakeLists.txt (新建)
cmake_minimum_required(VERSION 3.22)
project(Keyhunt LANGUAGES C CUDA)

# 提取BSGS算法
add_library(Keyhunt_bsgs STATIC
    keyhunt.c
    bloom.c
    util.c
)

target_include_directories(Keyhunt_bsgs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 提取CUDA哈希
add_library(Keyhunt_hash STATIC
    sha256.cu
    rmd160.cu
)

# 导出目标
add_library(Keyhunt::bsgs ALIAS Keyhunt_bsgs)
add_library(Keyhunt::hash ALIAS Keyhunt_hash)
```

**提取文件清单**:
- `keyhunt.c` - BSGS算法主逻辑
- `bloom.c` - Bloom Filter实现
- `util.c` - 工具函数
- `sha256.cu` - SHA256 CUDA实现
- `rmd160.cu` - RIPEMD160 CUDA实现

**工作量**: 16小时

---

**4. bitcoin-core/secp256k1 (CPU验证参考)**
```bash
# 克隆到external/目录
cd external/
git clone https://github.com/bitcoin-core/secp256k1.git
cd secp256k1
git checkout <latest-stable-tag>  # 锁定版本

# 记录版本信息
echo "bitcoin-core/secp256k1 $(git describe --tags)" >> ../../docs/reference-sources.md

# 编译安装
./autogen.sh
./configure --enable-module-recovery --enable-module-ecdh
make -j$(nproc)
sudo make install
```

**集成方案**:
```cmake
# 主CMakeLists.txt中查找
find_package(PkgConfig REQUIRED)
pkg_check_modules(SECP256K1 REQUIRED libsecp256k1)

# 链接到验证测试
target_link_libraries(sbps_validation_tests
    PRIVATE
        ${SECP256K1_LIBRARIES}
)

target_include_directories(sbps_validation_tests
    PRIVATE
        ${SECP256K1_INCLUDE_DIRS}
)
```

**用途**: CPU验证参考，确保GPU结果与权威实现一致

**工作量**: 8小时

---

**5. NCCL (NVIDIA/nccl) - 多GPU通信**
```bash
# 克隆到external/目录
cd external/
git clone https://github.com/NVIDIA/nccl.git
cd nccl
git checkout <latest-stable-tag>  # 锁定版本

# 记录版本信息
echo "NVIDIA/nccl $(git describe --tags)" >> ../../docs/reference-sources.md

# 编译安装
make -j$(nproc) src.build
sudo make install
```

**集成方案**:
```cmake
# 主CMakeLists.txt中查找
find_package(NCCL REQUIRED)

# 链接到多GPU调度器
target_link_libraries(sbps_gpu
    PRIVATE
        ${NCCL_LIBRARIES}
)

target_include_directories(sbps_gpu
    PRIVATE
        ${NCCL_INCLUDE_DIRS}
)
```

**用途**: 多GPU协同通信，实现95%扩展性

**工作量**: 8小时

---

**第三方库版本锁定文件（docs/reference-sources.md）**:
```markdown
# 第三方库参考源

本项目遵循"不造轮子"原则，复用以下经过验证的开源库：

## 核心算法库

### VanitySearch (JeanLucPons/VanitySearch)
- **版本**: v1.19 (commit: abc123...)
- **仓库**: https://github.com/JeanLucPons/VanitySearch
- **许可证**: GPL-3.0
- **用途**: GLV endomorphism、批量逆元、PTX优化
- **提取文件**: SECP256k1.cpp/h, IntGroup.cpp/h, GPU/GPUEngine.cu/h
- **克隆日期**: 2025-10-12
- **SHA-256**: ...

### BitCrack (brichard19/BitCrack)
- **版本**: v0.31 (commit: def456...)
- **仓库**: https://github.com/brichard19/BitCrack
- **许可证**: MIT
- **用途**: CUDA数学库、哈希引擎、批量密钥生成
- **提取文件**: cudaMath/*, hash/*, CudaKeySearchDevice/*
- **克隆日期**: 2025-10-12
- **SHA-256**: ...

### Keyhunt (albertobsd/keyhunt)
- **版本**: v1.0 (commit: ghi789...)
- **仓库**: https://github.com/albertobsd/keyhunt
- **许可证**: MIT
- **用途**: BSGS算法、Bloom Filter、Pollard Kangaroo
- **提取文件**: keyhunt.c, bloom.c, util.c, sha256.cu, rmd160.cu
- **克隆日期**: 2025-10-12
- **SHA-256**: ...

## 验证与通信库

### bitcoin-core/secp256k1
- **版本**: v0.4.0 (commit: jkl012...)
- **仓库**: https://github.com/bitcoin-core/secp256k1
- **许可证**: MIT
- **用途**: CPU验证参考（权威实现）
- **克隆日期**: 2025-10-12
- **SHA-256**: ...

### NVIDIA/nccl
- **版本**: v2.20.5 (commit: mno345...)
- **仓库**: https://github.com/NVIDIA/nccl
- **许可证**: BSD-3-Clause
- **用途**: 多GPU通信
- **克隆日期**: 2025-10-12
- **SHA-256**: ...

## 更新策略

- **版本锁定**: 所有库锁定到特定commit，确保可重现构建
- **安全更新**: 每月检查安全更新，评估后升级
- **兼容性测试**: 升级前必须通过完整测试套件
- **回滚机制**: 保留上一版本，支持快速回滚
```

---

**第三方库集成验证清单**:
1. ✅ 所有库克隆到external/目录
2. ✅ 版本锁定到特定commit
3. ✅ 记录SHA-256摘要
4. ✅ 编写CMake集成脚本
5. ✅ 编写单元测试验证提取功能
6. ✅ 文档化提取文件清单
7. ✅ 许可证合规性检查

**交付物**:
- external/VanitySearch/ (克隆仓库)
- external/BitCrack/ (克隆仓库)
- external/Keyhunt/ (克隆仓库)
- external/secp256k1/ (克隆仓库)
- external/nccl/ (克隆仓库)
- docs/reference-sources.md (版本锁定文件)
- external/*/CMakeLists.txt (集成脚本)
- tests/unit/external/ (提取功能验证测试)

**工作量**: 68小时

---

**阶段1总工作量（更新）**: 164小时（20.5个工作日）

---

### 阶段2: 核心层提取与适配（6周）

**任务2.1: ECC引擎提取（3周）**

**子任务**:
1. 提取VanitySearch GLV Endomorphism（1周）
   - 提取SECP256k1.cpp/h
   - 提取IntGroup.cpp/h
   - 编写适配器VanitySearchGLVAdapter
   - 编写单元测试

2. 提取BitCrack模运算（1周）
   - 提取cudaMath/secp256k1.cuh
   - 编写适配器BitCrackMathAdapter
   - 编写单元测试

3. 统一ECC接口（1周）
   - 设计IECCEngine接口
   - 实现UnifiedECCEngine
   - 编写集成测试
   - CPU/GPU一致性验证

**交付物**:
- Core/ECC/glv_endomorphism.cpp/h
- Core/ECC/batch_inverse.cpp/h
- Core/ECC/secp256k1_math.cuh
- Core/ECC/unified_ecc_engine.cpp/h
- 单元测试和集成测试

**工作量**: 120小时  

---

**任务2.2: 哈希引擎提取（2周）**

**子任务**:
1. 提取BitCrack哈希实现（1周）
   - 提取hash/sha256.cu
   - 提取hash/ripemd160.cu
   - 编写适配器BitCrackHashAdapter
   - 编写单元测试

2. 统一哈希接口（1周）
   - 设计IHashEngine接口
   - 实现UnifiedHashEngine
   - 编写集成测试
   - 性能基准测试

**交付物**:
- Core/Hash/sha256.cu/h
- Core/Hash/ripemd160.cu/h
- Core/Hash/unified_hash_engine.cpp/h
- 单元测试和性能测试

**工作量**: 80小时  

---

**任务2.3: 内存管理器设计（1周）**

**子任务**:
1. 设计SoA内存布局（3天）
2. 实现GPU内存池（2天）
3. 编写单元测试（2天）

**交付物**:
- Core/Memory/soa_layout.h
- Core/Memory/gpu_memory_pool.cpp/h
- 单元测试

**工作量**: 40小时  

---

### 阶段3: 算法层提取与实现（8周）

**任务3.1: 暴力搜索模式（3周）**

**子任务**:
1. 提取VanitySearch范围扫描（1周）
   - 提取GPU/GPUEngine.cu
   - 编写适配器
   - 编写单元测试

2. 提取BitCrack批量处理（1周）
   - 提取CudaDeviceKeys.cu
   - 编写适配器
   - 编写单元测试

3. 实现BruteForceStrategy（1周）
   - 实现策略类
   - 集成ECC和Hash引擎
   - 编写集成测试

**交付物**:
- Algorithm/BruteForce/range_scanner.cu/h
- Algorithm/BruteForce/batch_processor.cu/h
- Algorithm/BruteForce/brute_force_strategy.cpp/h
- 单元测试和集成测试

**工作量**: 120小时  

---

**任务3.2: BSGS模式（3周）**

**子任务**:
1. 提取Keyhunt BSGS算法（1.5周）
   - 提取keyhunt.c BSGS逻辑
   - 提取bloom.c Bloom Filter
   - 编写适配器
   - 编写单元测试

2. 实现BSGSStrategy（1.5周）
   - 实现策略类
   - 集成ECC引擎和Bloom Filter
   - 编写集成测试
   - 内存优化

**交付物**:
- Algorithm/BSGS/bsgs_algorithm.cpp/h
- Algorithm/BSGS/bloom_filter.cpp/h
- Algorithm/BSGS/bsgs_strategy.cpp/h
- 单元测试和集成测试

**工作量**: 120小时  

---

**任务3.3: Pollard Kangaroo模式（2周）**

**子任务**:
1. 提取Keyhunt Kangaroo算法（1周）
   - 提取kangaroo逻辑
   - 提取碰撞检测
   - 编写适配器

2. 实现PollardKangarooStrategy（1周）
   - 实现策略类
   - 集成ECC引擎
   - 编写集成测试

**交付物**:
- Algorithm/Kangaroo/kangaroo_algorithm.cpp/h
- Algorithm/Kangaroo/collision_detector.cpp/h
- Algorithm/Kangaroo/kangaroo_strategy.cpp/h
- 单元测试和集成测试

**工作量**: 80小时  

---

### 阶段4: GPU层实现（4周）

**任务4.1: 内核管理器（2周）**

**子任务**:
1. 提取VanitySearch PTX优化（1周）
2. 集成Cooperative Groups（3天）
3. 实现动态内核选择（4天）

**交付物**:
- GPU/Kernels/ecc_kernel.cu/h
- GPU/Kernels/hash_kernel.cu/h
- GPU/kernel_manager.cpp/h

**工作量**: 80小时  

---

**任务4.2: 多GPU调度器（2周）**

**子任务**:
1. 集成NCCL通信（1周）
2. 实现负载均衡（3天）
3. 实现容错恢复（4天）

**交付物**:
- GPU/multi_gpu_scheduler.cpp/h
- GPU/nccl_communicator.cpp/h
- GPU/fault_tolerance.cpp/h

**工作量**: 80小时  

---

### 阶段5: 存储层与接口层（3周）

**任务5.1: 检查点管理器（1周）**
**任务5.2: 结果管理器（1周）**
**任务5.3: CLI接口（1周）**

**工作量**: 120小时  

---

### 阶段6: 集成测试与优化（4周）

**任务6.1: 端到端测试（2周）**
**任务6.2: 性能优化（1周）**
**任务6.3: 文档完善（1周）**

**工作量**: 160小时  

---

## 📊 总工作量估算

| 阶段 | 工作量（小时） | 工作日 | 日历周 |
|------|--------------|--------|--------|
| 阶段1: 基础设施 | 40 | 5 | 1 |
| 阶段2: 核心层 | 240 | 30 | 6 |
| 阶段3: 算法层 | 320 | 40 | 8 |
| 阶段4: GPU层 | 160 | 20 | 4 |
| 阶段5: 存储与接口 | 120 | 15 | 3 |
| 阶段6: 集成与优化 | 160 | 20 | 4 |
| **总计** | **1040** | **130** | **26** |

**预计完成时间**: 6个月（按每周工作5天，每天8小时计算）

---

**计划制定人**: AI Agent (Augment Code)  
**计划版本**: v1.0  
**计划日期**: 2025-10-12  
**下次审查**: 每2周审查一次进度


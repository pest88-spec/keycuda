# Puzzle71Solver AI Agent 强制规范（铁笼协议 v5.0）

**文档版本**：v5.0  
**适用项目**：Puzzle71Solver CUDA Implementation (Feature Branch: `001-implement-puzzle71solver-mred`)  
**强制执行等级**：P0（所有规则为必须遵守，违反任何一条导致立即回滚）  
**生效日期**：2025-09-30  
**审查周期**：每次任务执行前必须重新确认

---

## 0. 适用范围与执行协议

### 0.1 约束对象

本规范强制约束以下所有参与者（人类开发者与AI Agent）：

**AI Agent角色**：

- **Developer Agent**：负责代码编写、CUDA内核实现、测试用例开发
- **Reviewer Agent**：负责代码审查、性能验证、确定性检查、安全审计
- **Executor Agent**：负责构建、测试执行、基准测试、Nsight分析、确定性重放验证
- **Fixer Agent**：负责CI失败修复、性能回归修复、确定性破坏修复

**人类参与者**：

- 项目维护者在审查AI产出时必须参照本规范
- 操作员在运行Puzzle71Solver时必须遵守CLI参数约束
- 审计人员在检查产物时必须验证摘要完整性

### 0.2 项目特殊约束（不可妥协）

Puzzle71Solver项目除通用AI Agent约束外，额外强制执行以下专项约束：

| 约束类别 | 强制要求 | 违反后果 |
|---------|---------|---------|
| **确定性重放** | 所有GPU运算必须可通过记录的配置完全重现 | 立即回滚+熔断 |
| **CPU/GPU一致性** | GPU结果必须与bitcoin-core/secp256k1 CPU实现一致（误差<1e-10） | 立即回滚 |
| **性能门槛** | RTX 2080 Ti必须≥1000M keys/sec | 阻止合并 |
| **测试优先** | 所有实现必须有先失败的测试 | 立即回滚 |
| **引用溯源** | 禁止重新实现ECC/BigInt，必须适配参考源 | 立即回滚+警告 |
| **防篡改摘要** | 所有artifact必须包含SHA-256摘要 | 阻止使用 |
| **操作员审计** | 所有运行必须记录operator-id和purpose | 阻止执行 |

### 0.3 宪法映射（Constitution Alignment）

本规范是项目宪法（Constitution）的可执行实现：

```
宪法原则 I    → 第3节（确定性重放约束）
宪法原则 II   → 第5节（测试驱动开发工作流）
宪法原则 III  → 第4节（性能门槛强制执行）
宪法原则 IV   → 第7节（防篡改与审计追踪）
宪法原则 V    → 第9节（文档自动化更新）
```

任何与宪法原则冲突的代码变更必须在设计阶段被拒绝。

---

## 1. 核心原则（铁律层 - L1）

### 1.1 DETERMINISM-FIRST 原则（确定性优先）

**规则**：所有GPU计算、随机数生成、数据结构遍历必须保证确定性重放。

**强制要求**：

```cpp
// ✅ 正确：使用固定种子和记录的配置
__global__ void batch_kernel(
    const uint256_t* scalars,
    ec_point_t* results,
    uint64_t replay_seed,    // 必须从配置加载
    int grid_dim,            // 必须从配置加载
    int block_dim            // 必须从配置加载
) {
    // 所有随机数生成必须使用replay_seed
    xorshift64_state rng = init_rng(replay_seed + blockIdx.x);
    // ...
}

// ❌ 错误：使用硬件时钟或未记录的随机源
__global__ void bad_kernel(const uint256_t* scalars, ec_point_t* results) {
    uint64_t rng = clock64();  // 不可重放！
    // ...
}
```

**CI检测器**：

```bash
#!/bin/bash
# ci/check_determinism.sh

VIOLATIONS=0

# 检查CUDA代码中的非确定性API
NON_DET_APIS=(
    "clock64" "clock" "curandGenerate" "rand()" "srand" "time(NULL)"
    "std::random_device" "std::chrono::high_resolution_clock"
)

for api in "${NON_DET_APIS[@]}"; do
    if grep -rn --include="*.cu" --include="*.cuh" "\b${api}\b" src/; then
        echo "ERROR: Non-deterministic API detected: ${api}"
        echo "  Use replay_seed from config instead"
        VIOLATIONS=$((VIOLATIONS + 1))
    fi
done

# 检查是否有未记录的launch配置
if grep -rn "<<<.*>>>" src/ | grep -v "grid_dim\|block_dim\|config"; then
    echo "ERROR: Kernel launch without config-based dimensions"
    VIOLATIONS=$((VIOLATIONS + 1))
fi

exit $VIOLATIONS
```

---

### 1.2 TEST-FIRST-CUDA 原则（测试优先CUDA）

**规则**：所有CUDA内核、host函数、CLI功能必须先编写失败的测试，确认测试失败后才能编写实现。

**强制工作流**：

```
[收到实现任务 T029: Implement fused batch kernel]
    ↓
[步骤1] 编写失败的测试
    - tests/unit/test_kernel_interfaces.cu
    - tests/validation/test_hash160_gpu_cpu_parity.cpp
    - tests/perf/test_range_scan_benchmark.cu
    ↓
[步骤2] 运行测试，确认全部失败（红灯）
    - 必须截图或保存日志作为证据
    - 提交到 docs/validation/evidence/T029_test_failures.log
    ↓
[步骤3] 编写最小实现
    - src/puzzle71_kernel.cu
    ↓
[步骤4] 运行测试，确认全部通过（绿灯）
    - 如仍有失败，回到步骤3
    ↓
[步骤5] 提交代码
    - Commit message必须引用测试失败证据文件
```

**CI门禁**：

```yaml
# .github/workflows/tdd-gate.yml
name: TDD Enforcement Gate

on:
  pull_request:
    paths:
      - 'src/**'
      - 'tests/**'

jobs:
  check-test-first:
    runs-on: ubuntu-latest
    steps:
      - name: Check test failure evidence
        run: |
          # 提取本次PR修改的源文件
          IMPL_FILES=$(git diff --name-only origin/main | grep "^src/")
          
          for impl_file in $IMPL_FILES; do
            # 提取任务ID（假设文件名或commit包含T0XX）
            TASK_ID=$(git log -1 --pretty=%B $impl_file | grep -oP 'T\d+')
            
            if [ -z "$TASK_ID" ]; then
              echo "ERROR: $impl_file missing task ID in commit message"
              exit 1
            fi
            
            # 检查是否存在测试失败证据
            EVIDENCE_FILE="docs/validation/evidence/${TASK_ID}_test_failures.log"
            if [ ! -f "$EVIDENCE_FILE" ]; then
              echo "ERROR: Missing test failure evidence for $TASK_ID"
              echo "  Expected: $EVIDENCE_FILE"
              exit 1
            fi
            
            # 验证证据文件时间戳早于实现文件
            EVIDENCE_TIME=$(git log -1 --format=%ct $EVIDENCE_FILE)
            IMPL_TIME=$(git log -1 --format=%ct $impl_file)
            
            if [ $EVIDENCE_TIME -gt $IMPL_TIME ]; then
              echo "ERROR: Test evidence created after implementation"
              echo "  This violates TDD (test-first) principle"
              exit 1
            fi
          done
          
          echo "✓ TDD compliance verified"
```

---

### 1.3 NO-CRYPTO-REINVENTION 原则（禁止重新实现密码学）

**规则**：禁止重新实现任何椭圆曲线运算、大数运算、哈希算法，必须适配参考源。

**强制参考源（SoT）**：

| 功能领域 | 参考源 | 提取位置 | 溯源方式 |
|---------|--------|---------|---------|
| Endomorphism | secp256k1-zkp | `src/KeyhuntCore/ecc/` | 文件头注释+LICENSE |
| Batch Stepping | VanitySearch | `src/KeyhuntCore/batch/` | 文件头注释+LICENSE |
| CPU Validation | bitcoin-core/secp256k1 | 系统库链接 | CMakeLists.txt |
| HASH160 | BitCrack | `src/KeyhuntCore/hash/` | 文件头注释+LICENSE |

**适配器模式强制执行**：

```cpp
// ✅ 正确：使用从参考源提取的代码
// src/KeyhuntCore/ecc/endomorphism.h

/**
 * Extracted from secp256k1-zkp endomorphism implementation
 * Original: https://github.com/ElementsProject/secp256k1-zkp
 * Original Path: src/scalar_impl.h::secp256k1_scalar_split_lambda
 * Original Commit: [记录在docs/reference-sources.md]
 * License: MIT
 * Extracted Date: 2025-10-05
 * Modifications: Adapted for Puzzle71Solver namespace
 */

namespace puzzle71 {
namespace ecc {

/**
 * @brief Lambda分解标量（从secp256k1-zkp提取）
 * @param scalar 输入标量
 * @param k1 输出分量1
 * @param k2 输出分量2
 */
inline void split_scalar_lambda(
    const secp256k1_scalar* scalar,
    secp256k1_scalar* k1,
    secp256k1_scalar* k2
) {
    // 从secp256k1-zkp提取的算法实现
    // 原始代码逻辑保持不变，仅调整命名空间
    secp256k1_scalar_split_lambda(k1, k2, scalar);
}

} // namespace ecc
} // namespace puzzle71

// ❌ 错误：重新实现endomorphism
namespace puzzle71 {
void my_endomorphism_split(uint256_t k, uint256_t* k1, uint256_t* k2) {
    // 自己实现GLV分解 - 严重违规！
}
}
```

**CI检测器**：

```bash
#!/bin/bash
# ci/check_crypto_reinvention.sh

VIOLATIONS=0

# 禁止的自定义实现标识
FORBIDDEN_IMPL=(
    "my_ec_mul" "custom_scalar_mul" "simple_point_add"
    "basic_modular_inverse" "quick_bigint" "fast_hash160"
    "optimized_ecdsa" "improved_secp256k1"
)

for pattern in "${FORBIDDEN_IMPL[@]}"; do
    if grep -rn --include="*.cpp" --include="*.cu" --include="*.h" "\b${pattern}\b" src/; then
        echo "ERROR: Detected crypto reinvention: ${pattern}"
        echo "  Extract from reference sources with proper attribution"
        VIOLATIONS=$((VIOLATIONS + 1))
    fi
done

# 检查是否有未经adapter的直接调用
if grep -rn "#include.*secp256k1.*\.h" src/ | grep -v "adapter\|bridge"; then
    echo "WARNING: Direct include of reference headers without adapter"
    echo "  Prefer using adapters in src/utils/*_adapter.h"
fi

exit $VIOLATIONS
```

---

### 1.4 ZERO-TOLERANCE-PERFORMANCE 原则（零容忍性能退化）

**规则**：所有性能关键路径的修改必须通过基准测试，不得低于已记录的基线。

**性能基线（Baseline Table）**：

```json
// benchmarks/baseline/gpu_baselines.json
{
  "version": "1.0",
  "updated_at": "2025-09-30T00:00:00Z",
  "baselines": [
    {
      "gpu_model": "NVIDIA GeForce RTX 2080 Ti",
      "min_keys_per_sec": 1000000000,
      "max_variance_pct": 5.0,
      "reference_occupancy": 85.0,
      "measured_at": "2025-09-25T10:00:00Z",
      "commit_sha": "abc123def456"
    },
    {
      "gpu_model": "NVIDIA GeForce RTX 3090",
      "min_keys_per_sec": 2000000000,
      "max_variance_pct": 5.0,
      "reference_occupancy": 90.0,
      "measured_at": "2025-09-25T10:00:00Z",
      "commit_sha": "abc123def456"
    },
    {
      "gpu_model": "NVIDIA A100-SXM4-40GB",
      "min_keys_per_sec": 4000000000,
      "max_variance_pct": 5.0,
      "reference_occupancy": 95.0,
      "measured_at": "2025-09-25T10:00:00Z",
      "commit_sha": "abc123def456"
    }
  ]
}
```

**CI性能门禁**：

```bash
#!/bin/bash
# ci/performance_gate.sh

set -e

GPU_MODEL=$(nvidia-smi --query-gpu=name --format=csv,noheader | head -n1)
BASELINE_FILE="benchmarks/baseline/gpu_baselines.json"

# 运行基准测试
echo "Running performance benchmark on ${GPU_MODEL}..."
./scripts/run-benchmarks.sh 0 5 > benchmarks/current_run.json

# 提取当前吞吐量
CURRENT_THROUGHPUT=$(jq '.median_keys_per_sec' benchmarks/current_run.json)

# 提取基线吞吐量
BASELINE_THROUGHPUT=$(jq -r \
    ".baselines[] | select(.gpu_model == \"${GPU_MODEL}\") | .min_keys_per_sec" \
    $BASELINE_FILE)

if [ -z "$BASELINE_THROUGHPUT" ]; then
    echo "WARNING: No baseline for ${GPU_MODEL}, recording current as baseline"
    exit 0
fi

# 计算比率
RATIO=$(echo "scale=4; $CURRENT_THROUGHPUT / $BASELINE_THROUGHPUT" | bc)
THRESHOLD=0.95

if (( $(echo "$RATIO < $THRESHOLD" | bc -l) )); then
    echo "❌ PERFORMANCE REGRESSION DETECTED"
    echo "  GPU Model:     ${GPU_MODEL}"
    echo "  Baseline:      ${BASELINE_THROUGHPUT} keys/sec"
    echo "  Current:       ${CURRENT_THROUGHPUT} keys/sec"
    echo "  Ratio:         ${RATIO}x (threshold: ${THRESHOLD}x)"
    echo ""
    echo "This PR is BLOCKED until performance is restored."
    exit 1
fi

echo "✓ Performance check passed: ${RATIO}x of baseline"
```

---

### 1.5 MANDATORY-DIGEST 原则（强制防篡改摘要）

**规则**：所有checkpoint、telemetry、benchmark、report文件必须包含SHA-256摘要。

**摘要格式标准**：

```json
// 所有artifact的统一摘要格式
{
  "payload": "... actual data ...",
  "digest": {
    "algorithm": "SHA-256",
    "hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    "timestamp": "2025-09-30T12:34:56Z",
    "generator": "Puzzle71Solver v1.0.0"
  }
}
```

**强制生成与验证工具**：

```cpp
// src/utils/digest_verifier.h

namespace puzzle71 {

/**
 * @brief 计算artifact的SHA-256摘要
 * @param data 原始数据
 * @param size 数据大小
 * @param out_digest 输出摘要（32字节）
 * @return 0成功，非0失败
 */
int compute_sha256_digest(const void* data, size_t size, uint8_t out_digest[32]);

/**
 * @brief 验证artifact的SHA-256摘要
 * @param artifact_path 文件路径
 * @param expected_digest 期望摘要（hex字符串）
 * @return 0成功，-1文件不存在，-2摘要不匹配
 * @sla 必须在250ms内完成验证
 */
int verify_artifact_digest(const char* artifact_path, const char* expected_digest);

/**
 * @brief 为artifact添加摘要字段
 * @param artifact_json JSON对象
 * @return 0成功，非0失败
 */
int append_digest_to_artifact(json& artifact_json);

} // namespace puzzle71
```

**CI摘要验证**：

```bash
#!/bin/bash
# ci/verify_all_digests.sh

set -e

echo "Verifying digests for all artifacts..."

FAILURES=0

# 验证checkpoint摘要
find checkpoints/ -name "*.json" | while read manifest; do
    if ! ./scripts/digest/verify-single-digest.sh "$manifest"; then
        echo "ERROR: Digest verification failed for $manifest"
        FAILURES=$((FAILURES + 1))
    fi
done

# 验证telemetry摘要
find telemetry/ -name "*.jsonl" | while read log; do
    # 每行telemetry都应该有内联摘要
    while read line; do
        if ! echo "$line" | jq -e '.digest' > /dev/null; then
            echo "ERROR: Missing digest in telemetry line"
            FAILURES=$((FAILURES + 1))
            break
        fi
    done < "$log"
done

# 验证benchmark摘要
if [ -f benchmarks/latest.json ]; then
    if ! jq -e '.digest' benchmarks/latest.json > /dev/null; then
        echo "ERROR: Missing digest in benchmark report"
        FAILURES=$((FAILURES + 1))
    fi
fi

if [ $FAILURES -gt 0 ]; then
    echo "❌ Digest verification failed with $FAILURES errors"
    exit 1
fi

echo "✓ All artifact digests verified"
```

---

## 2. 四级防御体系（分层约束）

### 2.1 L1层：铁律门禁（自动化强制执行）

以下违规立即触发CI失败并阻止合并：

| 检查项 | 检测脚本 | 违规后果 |
|--------|---------|---------|
| 确定性API使用 | `ci/check_determinism.sh` | 回滚+熔断 |
| TDD证据完整性 | `ci/tdd-gate.yml` | 阻止合并 |
| 密码学重新实现 | `ci/check_crypto_reinvention.sh` | 回滚+警告 |
| 性能基线 | `ci/performance_gate.sh` | 阻止合并 |
| 防篡改摘要 | `ci/verify_all_digests.sh` | 阻止使用 |
| 新文件白名单 | `ci/check_new_files.sh` | 阻止提交 |
| 占位符检测 | `ci/scan_placeholders.sh` | 阻止提交 |

### 2.2 L2层：工程层约束（自动扫描+人工复核）

以下违规触发警告并需要人工审查：

```bash
#!/bin/bash
# ci/engineering_checks.sh

WARNINGS=0

# 检查CUDA寄存器使用
echo "Checking CUDA register usage..."
if ! ./tools/nsight/check_register_budget.sh; then
    echo "WARNING: Kernel register usage >128 (may reduce occupancy)"
    WARNINGS=$((WARNINGS + 1))
fi

# 检查checkpoint加密强度
echo "Checking checkpoint encryption..."
if grep -rn "AES-128\|ChaCha20" src/; then
    echo "WARNING: Found weak encryption (require AES-256-GCM)"
    WARNINGS=$((WARNINGS + 1))
fi

# 检查操作员元数据记录
echo "Checking operator metadata..."
if ! grep -rn "operator_id\|operator_purpose" src/main.cpp; then
    echo "WARNING: Missing operator metadata logging"
    WARNINGS=$((WARNINGS + 1))
fi

# 检查确定性配置加载
echo "Checking config determinism..."
if grep -rn "rand()\|time(NULL)" src/config/; then
    echo "WARNING: Config loading may not be deterministic"
    WARNINGS=$((WARNINGS + 1))
fi

if [ $WARNINGS -gt 0 ]; then
    echo "⚠️  Engineering checks found $WARNINGS warnings"
    echo "Human review required before merge"
    exit 2  # 退出码2表示需要人工审查
fi

echo "✓ Engineering checks passed"
```

### 2.3 L3层：质量层约束（夜间批处理分析）

以下检查在夜间流水线执行，不阻止日间开发：

```bash
#!/bin/bash
# ci/nightly_quality_checks.sh

echo "=== Nightly Quality Analysis ==="

# 代码重复率分析
echo "[1/5] Analyzing code duplication..."
cloc --by-file --csv src/ > /tmp/cloc_report.csv
DUPLICATION=$(python3 tools/analyze_duplication.py /tmp/cloc_report.csv)
if (( $(echo "$DUPLICATION > 15" | bc -l) )); then
    echo "WARNING: Code duplication ${DUPLICATION}% exceeds 15% threshold"
fi

# 测试覆盖率趋势
echo "[2/5] Analyzing test coverage trends..."
./tools/coverage_trend.sh

# 性能趋势分析
echo "[3/5] Analyzing performance trends..."
python3 tools/plot_perf_trend.py \
    --input-dir benchmarks/history \
    --output-png reports/perf_trend_$(date +%Y%m%d).png

# 确定性重放验证（随机抽样）
echo "[4/5] Running random replay verification..."
RANDOM_MANIFEST=$(find checkpoints/ -name "*.json" | shuf -n 1)
if [ -f "$RANDOM_MANIFEST" ]; then
    ./scripts/replay/verify-replay.sh "$RANDOM_MANIFEST"
fi

# 摘要完整性审计
echo "[5/5] Auditing digest completeness..."
./scripts/digest/audit-digest-coverage.sh

echo "=== Nightly Quality Analysis Complete ==="
```

---

## 3. 确定性重放约束（专项强制）

### 3.1 可重放性要求清单

所有GPU运算必须满足以下可重放性条件：

| 组件 | 可重放要素 | 记录位置 | 验证方式 |
|------|-----------|---------|---------|
| CUDA Kernel | Grid/Block维度 | `config/puzzle71.yaml` | 配置重放 |
| RNG状态 | replay_seed | CheckpointManifest | 种子重放 |
| 输入数据 | KeyRangeShard边界 | CheckpointManifest | 边界比对 |
| 设备分配 | 设备ID列表 | TelemetryPacket | 设备匹配 |
| 环境变量 | CUDA_VISIBLE_DEVICES | 运行日志 | 环境重现 |

### 3.2 配置文件Schema

```yaml
# config/puzzle71.yaml

# 确定性重放配置
deterministic_config:
  version: "1.0"
  
  # Kernel启动配置（必须固定）
  kernel_launch:
    grid_dim: 1024        # 必须从配置加载，不得动态计算
    block_dim: 256        # 必须从配置加载，不得动态计算
    points_per_thread: 8  # 必须固定
    shared_mem_bytes: 49152
  
  # 随机数生成器（必须使用固定种子）
  rng:
    algorithm: "xorshift64"
    base_seed: 0x123456789ABCDEF0  # 从这个基础种子派生
  
  # 数据布局（必须固定）
  memory_layout:
    scalar_format: "little_endian_u32x8"
    point_format: "jacobian_projective"

# 操作员默认值
operator:
  default_id: "unknown"
  default_purpose: "testing"

# 性能配置
performance:
  checkpoint_interval_sec: 1800  # 30分钟
  checkpoint_interval_keys: 3000000000000  # 3e12
  telemetry_interval_sec: 1
```

### 3.3 确定性重放验证工具

```bash
#!/bin/bash
# scripts/replay/verify-replay.sh

set -e

MANIFEST=$1

if [ ! -f "$MANIFEST" ]; then
    echo "ERROR: Manifest not found: $MANIFEST"
    exit 1
fi

echo "=== Deterministic Replay Verification ==="
echo "Manifest: $MANIFEST"

# 提取原始运行的配置
ORIGINAL_SEED=$(jq -r '.replay_seed' "$MANIFEST")
ORIGINAL_SHARD_START=$(jq -r '.shard.start_key' "$MANIFEST")
ORIGINAL_SHARD_END=$(jq -r '.shard.end_key' "$MANIFEST")
ORIGINAL_DEVICE=$(jq -r '.device_id' "$MANIFEST")

echo "Original Config:"
echo "  Seed:   $ORIGINAL_SEED"
echo "  Shard:  $ORIGINAL_SHARD_START : $ORIGINAL_SHARD_END"
echo "  Device: $ORIGINAL_DEVICE"

# 运行重放
echo ""
echo "Running replay..."
REPLAY_OUTPUT="telemetry/replay_$(date +%s).jsonl"

./Puzzle71Solver \
    --replay-manifest "$MANIFEST" \
    --telemetry-jsonl "$REPLAY_OUTPUT" \
    --device "$ORIGINAL_DEVICE"

# 比对telemetry输出
echo ""
echo "Comparing telemetry outputs..."

ORIGINAL_TELEMETRY=$(jq -r '.telemetry_path' "$MANIFEST")

if [ ! -f "$ORIGINAL_TELEMETRY" ]; then
    echo "WARNING: Original telemetry not found, skipping comparison"
    exit 0
fi

# 逐行比对关键字段
python3 << EOF
import json

with open('$ORIGINAL_TELEMETRY') as f1, open('$REPLAY_OUTPUT') as f2:
    original_lines = [json.loads(line) for line in f1]
    replay_lines = [json.loads(line) for line in f2]
    
    if len(original_lines) != len(replay_lines):
        print(f"ERROR: Line count mismatch: {len(original_lines)} vs {len(replay_lines)}")
        exit(1)
    
    mismatches = 0
    for i, (orig, replay) in enumerate(zip(original_lines, replay_lines)):
        # 比对关键字段
        if orig['keys_per_sec'] != replay['keys_per_sec']:
            print(f"Line {i}: keys_per_sec mismatch")
            mismatches += 1
        
        if orig.get('hash_output') != replay.get('hash_output'):
            print(f"Line {i}: hash_output mismatch")
            mismatches += 1
    
    if mismatches > 0:
        print(f"ERROR: Found {mismatches} mismatches")
        exit(1)
    
    print("✓ Telemetry outputs are identical")
EOF

echo ""
echo "=== Replay Verification Complete ==="
```

### 3.4 配置加载强制校验

```cpp
// src/config/puzzle71_config.cpp

namespace puzzle71 {

struct DeterministicConfig {
    int grid_dim;
    int block_dim;
    int points_per_thread;
    uint64_t base_seed;
    std::string scalar_format;
    std::string point_format;
};

/**
 * @brief 加载确定性配置（必须在所有GPU操作前调用）
 * @param config_path 配置文件路径
 * @param out_config 输出配置
 * @return 0成功，非0失败
 * @note 配置必须完整，任何字段缺失都会导致加载失败
 */
int load_deterministic_config(
    const char* config_path,
    DeterministicConfig* out_config
) {
    // 读取YAML
    YAML::Node config = YAML::LoadFile(config_path);
    
    // 强制检查所有必需字段
    if (!config["deterministic_config"]) {
        fprintf(stderr, "ERROR: Missing deterministic_config section\n");
        return -1;
    }
    
    auto det = config["deterministic_config"];
    
    // 检查kernel_launch
    if (!det["kernel_launch"]) {
        fprintf(stderr, "ERROR: Missing kernel_launch config\n");
        return -1;
    }
    
    out_config->grid_dim = det["kernel_launch"]["grid_dim"].as<int>();
    out_config->block_dim = det["kernel_launch"]["block_dim"].as<int>();
    out_config->points_per_thread = det["kernel_launch"]["points_per_thread"].as<int>();
    
    // 检查RNG配置
    if (!det["rng"]) {
        fprintf(stderr, "ERROR: Missing rng config\n");
        return -1;
    }
    
    out_config->base_seed = det["rng"]["base_seed"].as<uint64_t>();
    
    // 验证配置合理性
    if (out_config->grid_dim <= 0 || out_config->block_dim <= 0) {
        fprintf(stderr, "ERROR: Invalid kernel dimensions\n");
        return -1;
    }
    
    if (out_config->block_dim > 1024) {
        fprintf(stderr, "ERROR: block_dim exceeds hardware limit (1024)\n");
        return -1;
    }
    
    return 0;
}

} // namespace puzzle71
```

---

## 4. 性能门槛强制执行

### 4.1 基准测试协议

**标准测试流程**：

```
1. 预热阶段：运行3个批次，丢弃结果
2. 测量阶段：运行5个批次，记录每个批次的吞吐量
3. 统计分析：计算中位数、标准差、最小值、最大值
4. 基线比对：与GPU型号对应的基线比较
5. 回归检测：与上一次测试结果比较
```

**基准测试脚本**：

```bash
#!/bin/bash
# scripts/run-benchmarks.sh

DEVICE_ID=${1:-0}
ITERATIONS=${2:-5}
WARMUP_ITERATIONS=3

OUTPUT_FILE="benchmarks/run_$(date +%Y%m%d_%H%M%S).json"

echo "=== Puzzle71Solver Performance Benchmark ==="
echo "Device: $DEVICE_ID"
echo "Iterations: $ITERATIONS (+ $WARMUP_ITERATIONS warmup)"
echo ""

# 获取GPU型号
GPU_MODEL=$(nvidia-smi --id=$DEVICE_ID --query-gpu=name --format=csv,noheader)
echo "GPU Model: $GPU_MODEL"

# 预热阶段
echo ""
echo "Warming up..."
for i in $(seq 1 $WARMUP_ITERATIONS); do
    echo "  Warmup $i/$WARMUP_ITERATIONS"
    ./Puzzle71Solver \
        --keyspace 0x400000000000000000:0x40000000000FFFFF \
        --target-address 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU \
        --operator-id benchmark \
        --operator-purpose warmup \
        --device $DEVICE_ID \
        --benchmark-mode \
        > /dev/null 2>&1
done

# 测量阶段
echo ""
echo "Measuring performance..."
THROUGHPUTS=()

for i in $(seq 1 $ITERATIONS); do
    echo "  Iteration $i/$ITERATIONS"
    
    RESULT=$(./Puzzle71Solver \
        --keyspace 0x400000000000000000:0x40000000000FFFFFF \
        --target-address 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU \
        --operator-id benchmark \
        --operator-purpose measurement \
        --device $DEVICE_ID \
        --benchmark-mode \
        2>&1 | grep "Throughput:" | awk '{print $2}')
    
    THROUGHPUTS+=($RESULT)
    echo "    Throughput: $RESULT keys/sec"
done

# 统计分析
MEDIAN=$(printf '%s\n' "${THROUGHPUTS[@]}" | sort -n | awk '{a[NR]=$1} END{print (NR%2==1)?a[(NR+1)/2]:(a[NR/2]+a[NR/2+1])/2}')
MIN=$(printf '%s\n' "${THROUGHPUTS[@]}" | sort -n | head -n1)
MAX=$(printf '%s\n' "${THROUGHPUTS[@]}" | sort -n | tail -n1)

# 计算标准差
STDDEV=$(python3 << EOF
import statistics
data = [${THROUGHPUTS[*]}]
print(statistics.stdev(data))
EOF
)

# 生成报告
cat > "$OUTPUT_FILE" << EOF
{
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "gpu_model": "$GPU_MODEL",
  "device_id": $DEVICE_ID,
  "iterations": $ITERATIONS,
  "warmup_iterations": $WARMUP_ITERATIONS,
  "throughputs": [$(IFS=,; echo "${THROUGHPUTS[*]}")],
  "statistics": {
    "median_keys_per_sec": $MEDIAN,
    "min_keys_per_sec": $MIN,
    "max_keys_per_sec": $MAX,
    "stddev": $STDDEV
  },
  "digest": {
    "algorithm": "SHA-256",
    "hash": "$(sha256sum "$OUTPUT_FILE" | awk '{print $1}')",
    "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  }
}
EOF

echo ""
echo "=== Benchmark Results ==="
echo "Median:  $MEDIAN keys/sec"
echo "Min:     $MIN keys/sec"
echo "Max:     $MAX keys/sec"
echo "StdDev:  $STDDEV"
echo ""
echo "Report saved to: $OUTPUT_FILE"

# 基线比对
BASELINE_FILE="benchmarks/baseline/gpu_baselines.json"
if [ -f "$BASELINE_FILE" ]; then
    BASELINE=$(jq -r ".baselines[] | select(.gpu_model == \"$GPU_MODEL\") | .min_keys_per_sec" "$BASELINE_FILE")
    
    if [ -n "$BASELINE" ] && [ "$BASELINE" != "null" ]; then
        RATIO=$(echo "scale=4; $MEDIAN / $BASELINE" | bc)
        echo "Baseline: $BASELINE keys/sec"
        echo "Ratio:    ${RATIO}x"
        
        if (( $(echo "$RATIO < 0.95" | bc -l) )); then
            echo ""
            echo "❌ PERFORMANCE REGRESSION"
            exit 1
        else
            echo ""
            echo "✓ Performance meets baseline"
        fi
    fi
fi
```

### 4.2 Nsight寄存器预算检查

```bash
#!/bin/bash
# tools/nsight/check_register_budget.sh

set -e

KERNEL_NAME="puzzle71_batch_kernel"
MAX_REGISTERS=128

echo "=== CUDA Register Budget Check ==="
echo "Kernel: $KERNEL_NAME"
echo "Budget: ≤$MAX_REGISTERS registers/thread"
echo ""

# 使用Nsight Compute分析寄存器使用
ncu --metrics launch__registers_per_thread \
    --target-processes all \
    --kernel-name "$KERNEL_NAME" \
    --export /tmp/ncu_registers \
    --force-overwrite \
    ./Puzzle71Solver \
        --keyspace 0x400000000000000000:0x40000000000FFFFF \
        --target-address 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU \
        --operator-id nsight \
        --operator-purpose register-check \
        --benchmark-mode

# 解析结果
REGISTERS=$(ncu --import /tmp/ncu_registers.ncu-rep \
    --csv \
    --page raw \
    | grep "launch__registers_per_thread" \
    | awk -F',' '{print $NF}')

echo "Measured: $REGISTERS registers/thread"

if [ "$REGISTERS" -gt "$MAX_REGISTERS" ]; then
    echo ""
    echo "❌ REGISTER BUDGET EXCEEDED"
    echo "  This will reduce occupancy and hurt performance"
    echo "  Action required: Optimize kernel to use fewer registers"
    exit 1
fi

echo ""
echo "✓ Register budget satisfied"
```

### 4.3 占用率监控

```cpp
// src/services/device_metrics.cpp

#include <nvml.h>

namespace puzzle71 {

struct DeviceMetrics {
    float sm_occupancy_pct;
    float memory_bandwidth_utilization_pct;
    uint64_t free_memory_bytes;
    uint64_t used_memory_bytes;
};

/**
 * @brief 获取设备性能指标
 * @param device_id CUDA设备ID
 * @param out_metrics 输出指标
 * @return 0成功，非0失败
 */
int get_device_metrics(int device_id, DeviceMetrics* out_metrics) {
    nvmlDevice_t device;
    nvmlReturn_t result;
    
    // 初始化NVML
    result = nvmlInit();
    if (result != NVML_SUCCESS) {
        return -1;
    }
    
    // 获取设备句柄
    result = nvmlDeviceGetHandleByIndex(device_id, &device);
    if (result != NVML_SUCCESS) {
        nvmlShutdown();
        return -1;
    }
    
    // 获取占用率
    nvmlUtilization_t utilization;
    result = nvmlDeviceGetUtilizationRates(device, &utilization);
    if (result == NVML_SUCCESS) {
        out_metrics->sm_occupancy_pct = utilization.gpu;
        out_metrics->memory_bandwidth_utilization_pct = utilization.memory;
    }
    
    // 获取内存使用
    nvmlMemory_t memory;
    result = nvmlDeviceGetMemoryInfo(device, &memory);
    if (result == NVML_SUCCESS) {
        out_metrics->free_memory_bytes = memory.free;
        out_metrics->used_memory_bytes = memory.used;
    }
    
    nvmlShutdown();
    return 0;
}

/**
 * @brief 检查性能告警条件
 * @param metrics 设备指标
 * @return true如果需要告警
 */
bool should_alert_performance(const DeviceMetrics& metrics) {
    // 占用率低于85%告警
    if (metrics.sm_occupancy_pct < 85.0f) {
        return true;
    }
    
    // 内存带宽利用率低于70%告警
    if (metrics.memory_bandwidth_utilization_pct < 70.0f) {
        return true;
    }
    
    return false;
}

} // namespace puzzle71
```

---

## 5. 测试驱动开发工作流（TDD门禁）

### 5.1 任务与测试映射

每个实现任务必须对应明确的测试文件：

| 任务ID | 实现文件 | 必需测试文件 | 测试类型 |
|--------|---------|-------------|---------|
| T029 | `src/puzzle71_kernel.cu` | `tests/unit/test_kernel_interfaces.cu`<br>`tests/validation/test_hash160_gpu_cpu_parity.cpp`<br>`tests/perf/test_range_scan_benchmark.cu` | 单元+验证+性能 |
| T031 | `src/utils/digest_verifier.cpp` | `tests/unit/test_digest_verifier.cpp`<br>`tests/perf/test_digest_verifier_perf.cpp` | 单元+性能 |
| T036 | `src/scheduler/range_scheduler.cpp` | `tests/integration/test_multi_gpu_partition.cpp`<br>`tests/contract/test_solver_keyspace.cpp` | 集成+契约 |
| T040 | `src/solver.cpp` | `tests/integration/test_gpu_determinism.cu`<br>`tests/contract/test_solver_target_address.cpp` | 集成+契约 |

### 5.2 测试模板

**单元测试模板**：

```cpp
// tests/unit/test_digest_verifier.cpp

#include <gtest/gtest.h>
#include "utils/digest_verifier.h"
#include <fstream>

class DigestVerifierTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建临时测试文件
        test_file_ = "test_artifact.json";
        std::ofstream ofs(test_file_);
        ofs << R"({"data": "test", "value": 123})";
        ofs.close();
        
        // 计算期望摘要
        expected_digest_ = "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08";
    }
    
    void TearDown() override {
        std::remove(test_file_.c_str());
    }
    
    std::string test_file_;
    std::string expected_digest_;
};

TEST_F(DigestVerifierTest, ComputeDigest_ValidFile_ReturnsCorrectHash) {
    uint8_t digest[32];
    
    // 读取文件
    std::ifstream ifs(test_file_, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(ifs)),
                               std::istreambuf_iterator<char>());
    
    int ret = puzzle71::compute_sha256_digest(data.data(), data.size(), digest);
    
    ASSERT_EQ(0, ret);
    
    // 转换为hex字符串
    char hex[65];
    for (int i = 0; i < 32; i++) {
        sprintf(&hex[i*2], "%02x", digest[i]);
    }
    
    EXPECT_EQ(expected_digest_, std::string(hex));
}

TEST_F(DigestVerifierTest, VerifyDigest_MatchingHash_ReturnsSuccess) {
    int ret = puzzle71::verify_artifact_digest(test_file_.c_str(), expected_digest_.c_str());
    
    EXPECT_EQ(0, ret);
}

TEST_F(DigestVerifierTest, VerifyDigest_MismatchedHash_ReturnsError) {
    std::string wrong_digest = "0000000000000000000000000000000000000000000000000000000000000000";
    
    int ret = puzzle71::verify_artifact_digest(test_file_.c_str(), wrong_digest.c_str());
    
    EXPECT_EQ(-2, ret);  // -2表示摘要不匹配
}

TEST_F(DigestVerifierTest, VerifyDigest_MissingFile_ReturnsError) {
    int ret = puzzle71::verify_artifact_digest("nonexistent.json", expected_digest_.c_str());
    
    EXPECT_EQ(-1, ret);  // -1表示文件不存在
}

TEST_F(DigestVerifierTest, VerifyDigest_Performance_MeetsSLA) {
    // 性能SLA：≤250ms
    auto start = std::chrono::high_resolution_clock::now();
    
    puzzle71::verify_artifact_digest(test_file_.c_str(), expected_digest_.c_str());
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    EXPECT_LE(duration.count(), 250) 
        << "Digest verification took " << duration.count() << "ms (SLA: ≤250ms)";
}
```

**CUDA验证测试模板**：

```cpp
// tests/validation/test_hash160_gpu_cpu_parity.cpp

#include <gtest/gtest.h>
#include "puzzle71_kernel.cu"
#include <secp256k1.h>  // CPU参考实现

class Hash160ParityTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 初始化GPU
        CUDA_CHECK(cudaSetDevice(0));
        
        // 初始化CPU参考库
        ctx_ = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    }
    
    void TearDown() override {
        secp256k1_context_destroy(ctx_);
        CUDA_CHECK(cudaDeviceReset());
    }
    
    secp256k1_context* ctx_;
};

TEST_F(Hash160ParityTest, GPU_CPU_Parity_1024Samples) {
    constexpr size_t SAMPLE_COUNT = 1024;
    
    // 生成随机标量
    std::vector<uint256_t> scalars(SAMPLE_COUNT);
    for (auto& scalar : scalars) {
        // 生成随机值（使用固定种子保证可重现）
        std::mt19937_64 rng(0x123456789ABCDEF0);
        for (int i = 0; i < 8; i++) {
            scalar.u32[i] = rng() & 0xFFFFFFFF;
        }
    }
    
    // GPU计算
    std::vector<uint160_t> gpu_hashes(SAMPLE_COUNT);
    uint256_t* d_scalars;
    uint160_t* d_hashes;
    
    CUDA_CHECK(cudaMalloc(&d_scalars, SAMPLE_COUNT * sizeof(uint256_t)));
    CUDA_CHECK(cudaMalloc(&d_hashes, SAMPLE_COUNT * sizeof(uint160_t)));
    CUDA_CHECK(cudaMemcpy(d_scalars, scalars.data(), 
                          SAMPLE_COUNT * sizeof(uint256_t), 
                          cudaMemcpyHostToDevice));
    
    // 调用GPU内核
    dim3 grid(32);
    dim3 block(256);
    puzzle71_hash160_kernel<<<grid, block>>>(d_scalars, d_hashes, SAMPLE_COUNT);
    CUDA_KERNEL_CHECK();
    
    CUDA_CHECK(cudaMemcpy(gpu_hashes.data(), d_hashes, 
                          SAMPLE_COUNT * sizeof(uint160_t), 
                          cudaMemcpyDeviceToHost));
    
    // CPU计算（使用bitcoin-core/secp256k1）
    std::vector<uint160_t> cpu_hashes(SAMPLE_COUNT);
    for (size_t i = 0; i < SAMPLE_COUNT; i++) {
        secp256k1_pubkey pubkey;
        
        // 标量乘法
        int ret = secp256k1_ec_pubkey_create(ctx_, &pubkey, 
                                              reinterpret_cast<const unsigned char*>(&scalars[i]));
        ASSERT_TRUE(ret);
        
        // 序列化为压缩公钥
        unsigned char serialized[33];
        size_t len = 33;
        secp256k1_ec_pubkey_serialize(ctx_, serialized, &len, &pubkey, 
                                       SECP256K1_EC_COMPRESSED);
        
        // 计算HASH160: RIPEMD160(SHA256(pubkey))
        unsigned char sha256_out[32];
        SHA256(serialized, len, sha256_out);
        
        unsigned char ripemd160_out[20];
        RIPEMD160(sha256_out, 32, ripemd160_out);
        
        memcpy(&cpu_hashes[i], ripemd160_out, 20);
    }
    
    // 逐个比对
    int mismatches = 0;
    for (size_t i = 0; i < SAMPLE_COUNT; i++) {
        if (memcmp(&gpu_hashes[i], &cpu_hashes[i], 20) != 0) {
            std::cout << "Mismatch at index " << i << std::endl;
            mismatches++;
            
            // 输出前10个不匹配的详细信息
            if (mismatches <= 10) {
                std::cout << "  GPU: ";
                for (int j = 0; j < 20; j++) {
                    printf("%02x", gpu_hashes[i].u8[j]);
                }
                std::cout << std::endl;
                
                std::cout << "  CPU: ";
                for (int j = 0; j < 20; j++) {
                    printf("%02x", cpu_hashes[i].u8[j]);
                }
                std::cout << std::endl;
            }
        }
    }
    
    EXPECT_EQ(0, mismatches) 
        << "Found " << mismatches << " mismatches out of " << SAMPLE_COUNT << " samples";
    
    // 清理
    cudaFree(d_scalars);
    cudaFree(d_hashes);
}
```

---

## 6. 引用溯源与代码提取规范

### 6.1 代码提取前置检查清单

在从参考源提取代码前必须确认以下事项：

**强制要求（P0级，违反立即回滚）**：

1. ✅ **许可证兼容性检查**
   - 仅允许MIT/BSD/Apache 2.0等宽松许可证
   - 复制完整许可证文本到 `docs/licenses/LICENSE-<ProjectName>.txt`
   - 在提取文件头部添加SPDX标识符

2. ✅ **禁止重新造轮子检查**
   - 必须优先搜索现有项目中是否已有类似实现
   - 检查参考源（BitCrack/VanitySearch/secp256k1-zkp）是否有可用代码
   - **严禁**自己实现任何ECC/BigInt/Hash算法

3. ✅ **溯源信息记录**
   - 记录原始项目URL
   - 记录原始commit hash
   - 记录原始文件路径和行号范围
   - 记录提取日期和修改说明

**溯源记录文档**：

所有提取必须在 `docs/reference-sources.md` 中记录：

```markdown
# Reference Sources Extraction Log

| 提取文件 | 原始项目 | 原始路径 | Commit Hash | 提取日期 | 修改说明 |
|---------|---------|---------|------------|---------|---------|
| src/KeyhuntCore/hash/sha256.cu | BitCrack | CudaKeySearchDevice/sha256.cu | abc123def | 2025-10-05 | 调整命名空间 |
| src/KeyhuntCore/ecc/endomorphism.cu | secp256k1-zkp | src/scalar_impl.h | def456abc | 2025-10-05 | CUDA移植 |
```

### 6.2 提取文件溯源注释规范

**强制模板**（每个提取文件头部必须包含）：

```cpp
/**
 * Extracted from <ProjectName> by <Author>
 *
 * @origin       <https://github.com/user/project>
 * @origin_path  <src/original/file.cpp>
 * @origin_commit <abc123def456> (记录在docs/reference-sources.md)
 * @origin_license MIT
 *
 * @extracted_date   2025-10-05
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Adapted for CUDA device code, namespace adjustment
 * @reuse_check_L1   当前项目: 无现有实现
 * @reuse_check_L2   <ProjectName>: <path::function>
 * @sot_ref          SOT-CRYPTO: <project/file.cpp L123-L456>
 *
 * @spdx_license_identifier MIT
 */

// ❌ 错误示例：缺少溯源注释
// namespace puzzle71 {
//     void my_sha256(...) { ... }  // 严重违规：无溯源信息
// }

// ✅ 正确示例：完整溯源注释
/**
 * Extracted from BitCrack by brichard19
 * @origin https://github.com/brichard19/BitCrack
 * @origin_path CudaKeySearchDevice/sha256.cu
 * @origin_commit 7a8b9c0d1e2f3456
 * @origin_license MIT
 * @extracted_date 2025-10-05
 * @modifications Namespace change: global → puzzle71::hash
 */
namespace puzzle71 {
namespace hash {
    __device__ void sha256_transform(...) {
        // BitCrack原始实现，仅调整命名空间
    }
}
}
```

### 6.3 许可证文件管理

**许可证存储规范**：

```
docs/licenses/
├── LICENSE-BitCrack.txt          # BitCrack完整MIT许可证
├── LICENSE-CudaBrainSecp.txt     # CudaBrainSecp许可证
├── LICENSE-VanitySearch.txt      # VanitySearch许可证
└── LICENSE-secp256k1-zkp.txt     # secp256k1-zkp许可证
```

**CI检查脚本**：

```bash
#!/bin/bash
# ci/check_extraction_attribution.sh

echo "Checking code extraction attribution..."

VIOLATIONS=0

# 检查所有源文件是否包含溯源注释
for file in $(find src/ -name "*.cu" -o -name "*.cpp" -o -name "*.h"); do
    # 跳过自己实现的文件（需要在docs/original-implementations.txt中声明）
    if grep -q "^${file}$" docs/original-implementations.txt 2>/dev/null; then
        continue
    fi

    # 检查是否有@origin标记
    if ! grep -q "@origin" "$file"; then
        echo "WARNING: Missing @origin attribution in $file"
        echo "  If this is original work, add to docs/original-implementations.txt"
        echo "  If extracted from reference, add @origin/@origin_commit tags"
        VIOLATIONS=$((VIOLATIONS + 1))
    fi

    # 检查是否有@spdx_license_identifier
    if ! grep -q "@spdx_license_identifier" "$file"; then
        echo "ERROR: Missing @spdx_license_identifier in $file"
        VIOLATIONS=$((VIOLATIONS + 1))
    fi
done

# 检查许可证文件是否存在
REQUIRED_LICENSES=("BitCrack" "CudaBrainSecp" "VanitySearch" "secp256k1-zkp")

for license in "${REQUIRED_LICENSES[@]}"; do
    if [ ! -f "docs/licenses/LICENSE-${license}.txt" ]; then
        echo "ERROR: Missing license file: docs/licenses/LICENSE-${license}.txt"
        VIOLATIONS=$((VIOLATIONS + 1))
    fi
done

if [ $VIOLATIONS -gt 0 ]; then
    echo ""
    echo "❌ Attribution check failed with $VIOLATIONS violations"
    exit 1
fi

echo "✓ All code extractions properly attributed"
```

### 6.4 禁止简化/虚拟/占位实现

**严格禁止的实现模式**：

```cpp
// ❌ 绝对禁止：简化实现
__device__ void quick_ec_mul(uint256_t* k, ec_point_t* p) {
    // 简化版本的点乘 - 严重违规！
    // 必须使用完整的参考实现
}

// ❌ 绝对禁止：虚拟/Stub实现
__device__ void hash160(const uint8_t* pubkey, uint8_t* hash) {
    // TODO: 实现HASH160 - 严重违规！
    memset(hash, 0, 20);  // 占位代码
}

// ❌ 绝对禁止：假数据生成
__device__ void validate_signature(uint256_t* sig) {
    return true;  // 虚假验证 - 严重违规！
}

// ✅ 正确：完整提取参考实现
/**
 * Extracted from BitCrack::HASH160
 * @origin https://github.com/brichard19/BitCrack
 * @origin_path CudaKeySearchDevice/sha256.cu + ripemd160.cu
 */
__device__ void hash160(const uint8_t* pubkey, size_t len, uint8_t* hash) {
    uint8_t sha256_out[32];
    sha256_transform(pubkey, len, sha256_out);  // BitCrack完整实现
    ripemd160_transform(sha256_out, 32, hash);  // BitCrack完整实现
}
```

**CI占位符检测**：

```bash
#!/bin/bash
# ci/scan_placeholders.sh

echo "Scanning for placeholder/stub implementations..."

VIOLATIONS=0

# 检测占位符关键词
PLACEHOLDER_PATTERNS=(
    "TODO.*implement"
    "FIXME.*stub"
    "placeholder"
    "dummy.*implementation"
    "simplified.*version"
    "mock.*data"
    "fake.*result"
)

for pattern in "${PLACEHOLDER_PATTERNS[@]}"; do
    if grep -rn --include="*.cu" --include="*.cpp" -iE "$pattern" src/; then
        echo "ERROR: Found placeholder/stub code: $pattern"
        VIOLATIONS=$((VIOLATIONS + 1))
    fi
done

if [ $VIOLATIONS -gt 0 ]; then
    echo ""
    echo "❌ Placeholder detection failed with $VIOLATIONS violations"
    echo "  All implementations must be complete and extracted from reference sources"
    exit 1
fi

echo "✓ No placeholders found"
```

---

## 7. 防篡改与审计追踪

### 7.1 操作员元数据强制记录

**CLI参数强制检查**：

```cpp
// src/main.cpp

int main(int argc, char** argv) {
    // 解析CLI参数
    CLI::App app{"Puzzle71Solver - Bitcoin Puzzle #71 CUDA Solver"};
    
    // 必需参数
    std::string keyspace;
    std::string target_address;
    std::string operator_id;
    std::string operator_purpose;
    
    app.add_option("--keyspace", keyspace, "Key range (start:end)")
        ->required();
    app.add_option("--target-address", target_address, "Target Bitcoin address")
        ->required();
    app.add_option("--operator-id", operator_id, "Operator identifier")
        ->required();
    app.add_option("--operator-purpose", operator_purpose, "Purpose of this run")
        ->required();
    
    CLI11_PARSE(app, argc, argv);
    
    // 验证必需参数
    if (keyspace.empty() || target_address.empty() || 
        operator_id.empty() || operator_purpose.empty()) {
        fprintf(stderr, "ERROR: Missing required parameters\n");
        fprintf(stderr, "  All of --keyspace, --target-address, --operator-id, ");
        fprintf(stderr, "--operator-purpose must be provided\n");
        return 1;
    }
    
    // 记录审计日志
    log_audit_entry(operator_id, operator_purpose, keyspace, target_address);
    
    // 继续执行...
}
```

### 7.2 WORM审计日志

```cpp
// src/utils/audit_logger.cpp

#include <fstream>
#include <chrono>
#include <unistd.h>  // gethostname

namespace puzzle71 {

/**
 * @brief 记录审计日志条目
 * @param operator_id 操作员ID
 * @param operator_purpose 操作目的
 * @param keyspace 密钥空间
 * @param target_address 目标地址
 * @return 0成功，非0失败
 * @note 审计日志写入到append-only文件，5秒内完成
 */
int log_audit_entry(
    const std::string& operator_id,
    const std::string& operator_purpose,
    const std::string& keyspace,
    const std::string& target_address
) {
    // 获取时间戳
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::system_clock::to_time_t(now);
    
    // 获取主机名
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    
    // 构造审计记录
    json audit_entry = {
        {"timestamp", timestamp},
        {"hostname", hostname},
        {"operator_id", operator_id},
        {"operator_purpose", operator_purpose},
        {"keyspace", keyspace},
        {"target_address", target_address},
        {"process_id", getpid()}
    };
    
    // 计算摘要
    std::string entry_str = audit_entry.dump();
    uint8_t digest[32];
    compute_sha256_digest(entry_str.c_str(), entry_str.size(), digest);
    
    char digest_hex[65];
    for (int i = 0; i < 32; i++) {
        sprintf(&digest_hex[i*2], "%02x", digest[i]);
    }
    
    audit_entry["digest"] = std::string(digest_hex);
    
    // 写入WORM日志（append-only）
    auto start = std::chrono::high_resolution_clock::now();
    
    std::ofstream audit_log("logs/audit.jsonl", std::ios::app);
    if (!audit_log.is_open()) {
        fprintf(stderr, "ERROR: Failed to open audit log\n");
        return -1;
    }
    
    audit_log << audit_entry.dump() << std::endl;
    audit_log.flush();
    audit_log.close();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // 检查SLA（5秒）
    if (duration.count() > 5000) {
        fprintf(stderr, "WARNING: Audit log write took %ldms (SLA: <5000ms)\n", 
                duration.count());
    }
    
    return 0;
}

/**
 * @brief 验证审计日志完整性
 * @param audit_log_path 审计日志路径
 * @return 0成功，非0失败
 */
int verify_audit_log_integrity(const char* audit_log_path) {
    std::ifstream log_file(audit_log_path);
    if (!log_file.is_open()) {
        return -1;
    }
    
    int line_num = 0;
    int failures = 0;
    std::string line;
    
    while (std::getline(log_file, line)) {
        line_num++;
        
        json entry = json::parse(line);
        
        // 提取存储的摘要
        std::string stored_digest = entry["digest"];
        entry.erase("digest");
        
        // 重新计算摘要
        std::string entry_str = entry.dump();
        uint8_t computed_digest[32];
        compute_sha256_digest(entry_str.c_str(), entry_str.size(), computed_digest);
        
        char computed_hex[65];
        for (int i = 0; i < 32; i++) {
            sprintf(&computed_hex[i*2], "%02x", computed_digest[i]);
        }
        
        // 比对
        if (stored_digest != std::string(computed_hex)) {
            fprintf(stderr, "ERROR: Audit log integrity check failed at line %d\n", line_num);
            failures++;
        }
    }
    
    return (failures == 0) ? 0 : -1;
}

} // namespace puzzle71
```

### 7.3 摘要性能基准测试

```cpp
// tests/perf/test_digest_verifier_perf.cpp

#include <gtest/gtest.h>
#include "utils/digest_verifier.h"
#include <chrono>

class DigestPerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建测试文件（不同大小）
        create_test_file("small.json", 1024);        // 1 KB
        create_test_file("medium.json", 102400);     // 100 KB
        create_test_file("large.json", 1048576);     // 1 MB
        create_test_file("xlarge.json", 10485760);   // 10 MB
    }
    
    void create_test_file(const char* path, size_t size) {
        std::ofstream ofs(path, std::ios::binary);
        std::vector<uint8_t> data(size, 0x42);
        ofs.write(reinterpret_cast<const char*>(data.data()), size);
        ofs.close();
        
        // 预计算摘要
        uint8_t digest[32];
        puzzle71::compute_sha256_digest(data.data(), size, digest);
        
        char hex[65];
        for (int i = 0; i < 32; i++) {
            sprintf(&hex[i*2], "%02x", digest[i]);
        }
        
        expected_digests_[path] = std::string(hex);
    }
    
    std::map<std::string, std::string> expected_digests_;
};

TEST_F(DigestPerformanceTest, SmallFile_MeetsSLA) {
    // SLA: ≤250ms
    auto start = std::chrono::high_resolution_clock::now();
    
    int ret = puzzle71::verify_artifact_digest(
        "small.json", 
        expected_digests_["small.json"].c_str()
    );
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    ASSERT_EQ(0, ret);
    EXPECT_LE(duration.count(), 250)
        << "Small file verification took " << duration.count() << "ms (SLA: ≤250ms)";
}

TEST_F(DigestPerformanceTest, MediumFile_MeetsSLA) {
    // SLA: ≤250ms
    auto start = std::chrono::high_resolution_clock::now();
    
    int ret = puzzle71::verify_artifact_digest(
        "medium.json", 
        expected_digests_["medium.json"].c_str()
    );
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    ASSERT_EQ(0, ret);
    EXPECT_LE(duration.count(), 250)
        << "Medium file verification took " << duration.count() << "ms (SLA: ≤250ms)";
}

TEST_F(DigestPerformanceTest, LargeFile_MeetsSLA) {
    // SLA: ≤250ms
    auto start = std::chrono::high_resolution_clock::now();
    
    int ret = puzzle71::verify_artifact_digest(
        "large.json", 
        expected_digests_["large.json"].c_str()
    );
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    ASSERT_EQ(0, ret);
    EXPECT_LE(duration.count(), 250)
        << "Large file verification took " << duration.count() << "ms (SLA: ≤250ms)";
}

TEST_F(DigestPerformanceTest, XLargeFile_MeetsSLA) {
    // SLA: ≤250ms（10MB文件可能需要放宽到500ms）
    auto start = std::chrono::high_resolution_clock::now();
    
    int ret = puzzle71::verify_artifact_digest(
        "xlarge.json", 
        expected_digests_["xlarge.json"].c_str()
    );
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    ASSERT_EQ(0, ret);
    EXPECT_LE(duration.count(), 500)
        << "XLarge file verification took " << duration.count() << "ms (SLA: ≤500ms)";
}
```

---

## 8. CI流水线（CUDA项目专项）

### 8.1 完整CI流水线

```yaml
# .github/workflows/puzzle71-ci.yml
name: Puzzle71Solver CI Gate

on:
  pull_request:
    branches: [main, develop, 001-implement-puzzle71solver-mred]
  push:
    branches: [main]

jobs:
  # 阶段1: 代码提取溯源验证
  extraction-attribution-check:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Check code extraction attribution
        run: ./ci/check_extraction_attribution.sh

      - name: Verify license files
        run: |
          ./ci/verify_license_files.sh

      - name: Scan for placeholder code
        run: ./ci/scan_placeholders.sh

  # 阶段2: 静态分析
  static-analysis:
    runs-on: ubuntu-latest
    needs: extraction-attribution-check
    steps:
      - uses: actions/checkout@v3
      
      - name: Scan for placeholders
        run: ./ci/scan_placeholders.sh
      
      - name: Check determinism violations
        run: ./ci/check_determinism.sh
      
      - name: Check crypto reinvention
        run: ./ci/check_crypto_reinvention.sh
      
      - name: Check new files
        run: ./ci/check_new_files.sh
      
      - name: Verify TDD compliance
        run: ./ci/tdd-gate.yml

  # 阶段3: 编译与单元测试（CPU）
  build-and-test-cpu:
    runs-on: ubuntu-latest
    needs: static-analysis
    steps:
      - uses: actions/checkout@v3
      
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake g++ libssl-dev libgtest-dev
      
      - name: Build project
        run: |
          mkdir build && cd build
          cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_CPU_TESTS=ON
          make -j$(nproc)
      
      - name: Run unit tests (CPU)
        run: |
          cd build
          ctest --output-on-failure --timeout 300 -R "unit|contract"
      
      - name: Check test coverage
        run: |
          ./tools/check_coverage.sh
          coverage=$(cat build/coverage.txt | grep 'lines' | awk '{print $2}' | tr -d '%')
          if [ "$coverage" -lt 80 ]; then
            echo "ERROR: Coverage $coverage% < 80%"
            exit 1
          fi

  # 阶段4: CUDA编译与GPU测试
  build-and-test-gpu:
    runs-on: [self-hosted, gpu, cuda]
    needs: static-analysis
    steps:
      - uses: actions/checkout@v3
      
      - name: Check CUDA environment
        run: |
          nvidia-smi
          nvcc --version
      
      - name: Build project with CUDA
        run: |
          mkdir build && cd build
          cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_CUDA=ON
          make -j$(nproc)
      
      - name: Run GPU unit tests
        run: |
          cd build
          ctest --output-on-failure --timeout 600 -R "gpu"
      
      - name: Run CUDA validation tests
        run: |
          cd build
          ctest --output-on-failure --timeout 600 -R "validation"
      
      - name: Check register budget
        run: ./tools/nsight/check_register_budget.sh

  # 阶段5: 确定性重放验证
  determinism-check:
    runs-on: [self-hosted, gpu, cuda]
    needs: build-and-test-gpu
    steps:
      - uses: actions/checkout@v3
      
      - name: Build project
        run: |
          mkdir build && cd build
          cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_CUDA=ON
          make -j$(nproc)
      
      - name: Run determinism test
        run: |
          cd build
          ctest --output-on-failure --timeout 1200 -R "determinism"
      
      - name: Verify replay integrity
        run: |
          # 查找最近的checkpoint
          LATEST_CHECKPOINT=$(ls -t checkpoints/*.json | head -n1)
          if [ -f "$LATEST_CHECKPOINT" ]; then
            ./scripts/replay/verify-replay.sh "$LATEST_CHECKPOINT"
          fi

  # 阶段6: 性能基准测试
  performance-benchmark:
    runs-on: [self-hosted, gpu, cuda]
    needs: build-and-test-gpu
    steps:
      - uses: actions/checkout@v3
      
      - name: Build project
        run: |
          mkdir build && cd build
          cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_CUDA=ON
          make -j$(nproc)
      
      - name: Run benchmark
        run: |
          cd build
          ../scripts/run-benchmarks.sh 0 5
      
      - name: Check performance regression
        run: ./ci/performance_gate.sh
      
      - name: Archive benchmark results
        uses: actions/upload-artifact@v3
        with:
          name: benchmark-results
          path: benchmarks/*.json

  # 阶段7: 摘要完整性验证
  digest-verification:
    runs-on: ubuntu-latest
    needs: [build-and-test-cpu, build-and-test-gpu]
    steps:
      - uses: actions/checkout@v3
      
      - name: Verify all artifact digests
        run: ./ci/verify_all_digests.sh
      
      - name: Check digest performance
        run: |
          cd build
          ctest --output-on-failure -R "digest_verifier_perf"

  # 阶段8: 最终门禁
  gate-summary:
    runs-on: ubuntu-latest
    needs: [reference-sync, static-analysis, build-and-test-cpu, 
            build-and-test-gpu, determinism-check, 
            performance-benchmark, digest-verification]
    steps:
      - name: Generate gate report
        run: |
          cat > gate_report.md << EOF
          # CI Gate Summary

          All checks passed ✓

          ## Executed Checks
          - Reference source sync and verification
          - Static analysis (placeholders, determinism, crypto reinvention)
          - TDD compliance (test evidence verification)
          - CPU unit tests (coverage ≥80%)
          - GPU unit tests + validation
          - CUDA register budget (≤128 registers/thread)
          - Deterministic replay verification
          - Performance baseline (RTX 2080 Ti ≥1000M keys/sec)
          - Artifact digest verification (SLA ≤250ms)

          ## Next Steps
          Ready for human review and merge.
          EOF
      
      - name: Post to PR
        uses: actions/github-script@v6
        with:
          script: |
            github.rest.issues.createComment({
              issue_number: context.issue.number,
              owner: context.repo.owner,
              repo: context.repo.repo,
              body: require('fs').readFileSync('gate_report.md', 'utf8')
            })
```

---

## 9. 开发者与审查者检查表

### 9.1 Developer提交前检查表

```markdown
# Developer Pre-Commit Checklist (Puzzle71Solver)

## 确定性约束检查
- [ ] 所有RNG使用固定种子（从config/puzzle71.yaml加载）
- [ ] 所有kernel launch使用配置文件中的grid/block维度
- [ ] 未使用clock64(), time(NULL), std::random_device等非确定性API
- [ ] 已测试重放验证通过（scripts/replay/verify-replay.sh）

## 测试驱动开发（TDD）
- [ ] 已编写失败的测试用例（保存失败日志到docs/validation/evidence/）
- [ ] 测试失败证据的时间戳早于实现代码
- [ ] 所有测试现在通过（绿灯）
- [ ] 测试覆盖率≥80%

## 引用溯源
- [ ] 已记录代码提取溯源信息（docs/reference-sources.md）
- [ ] 文件头包含完整@origin注释
- [ ] 所有ECC/BigInt代码通过adapter调用参考源
- [ ] 已在函数注释中标注@reuse_check和@sot_ref

## 性能要求
- [ ] 已运行scripts/run-benchmarks.sh
- [ ] RTX 2080 Ti吞吐量≥1000M keys/sec（或对应GPU基线）
- [ ] 性能不低于baseline的95%
- [ ] CUDA寄存器使用≤128/thread（tools/nsight/check_register_budget.sh）

## 防篡改与审计
- [ ] 所有artifact包含SHA-256摘要
- [ ] 摘要验证性能≤250ms（tests/perf/test_digest_verifier_perf.cpp）
- [ ] CLI参数包含--operator-id和--operator-purpose
- [ ] 审计日志记录完整（logs/audit.jsonl）

## 代码质量
- [ ] 无TODO/FIXME/placeholder标记（ci/scan_placeholders.sh）
- [ ] 无新增未授权文件（ci/check_new_files.sh）
- [ ] 所有CUDA API使用CUDA_CHECK包装
- [ ] 编译零警告（-Wall -Wextra -Werror）

## 提交准备
- [ ] Commit message包含任务ID（T0XX）
- [ ] Commit message引用测试失败证据文件
- [ ] 已更新相关文档（quickstart.md, performance.md等）
- [ ] 已填写本检查表并附在PR描述中
```

### 9.2 Reviewer审查检查表

```markdown
# Reviewer Checklist (Puzzle71Solver)

## 第一轮：宪法合规性审查
- [ ] 确定性重放：所有GPU运算使用固定配置和种子
- [ ] 测试优先：存在测试失败证据且时间戳早于实现
- [ ] CPU/GPU一致性：存在验证测试并通过
- [ ] 性能门槛：基准测试结果满足baseline要求
- [ ] 操作员审计：CLI强制要求operator-id和purpose

## 第二轮：引用溯源审查
- [ ] 提取代码包含完整溯源信息（docs/reference-sources.md已更新）
- [ ] 许可证文件已复制到docs/licenses/
- [ ] 所有密码学操作通过adapter调用参考源
- [ ] @reuse_check注释记录5级检查结果
- [ ] @sot_ref引用具体的参考源位置

## 第三轮：质量审查
- [ ] 无占位符标记（扫描通过ci/scan_placeholders.sh）
- [ ] 无非确定性API（扫描通过ci/check_determinism.sh）
- [ ] 无密码学重新实现（扫描通过ci/check_crypto_reinvention.sh）
- [ ] 所有CUDA API包装CUDA_CHECK宏
- [ ] 函数有完整Doxygen注释

## 第四轮：测试审查
- [ ] TDD证据文件存在且有效
- [ ] 单元测试覆盖≥80%
- [ ] 验证测试通过（GPU vs CPU parity）
- [ ] 性能测试通过（≥baseline的95%）
- [ ] 摘要验证测试通过（≤250ms SLA）

## 第五轮：确定性审查
- [ ] config/puzzle71.yaml包含完整确定性配置
- [ ] Kernel launch使用配置文件参数
- [ ] RNG使用replay_seed初始化
- [ ] 重放验证脚本测试通过
- [ ] Telemetry输出可重现

## 第六轮：性能审查
- [ ] 基准测试结果已归档
- [ ] 性能满足GPU型号对应的baseline
- [ ] CUDA寄存器预算≤128/thread
- [ ] 占用率≥85%（期望值）
- [ ] 无性能回归（与上次比较）

## 第七轮：安全与审计审查
- [ ] 所有artifact包含SHA-256摘要
- [ ] 摘要验证SLA满足≤250ms
- [ ] 审计日志记录完整
- [ ] 操作员元数据正确传递
- [ ] 敏感数据无泄露（检查日志输出）

## 审查决策
- [ ] APPROVE（通过）
- [ ] REQUEST_CHANGES（需修改）
- [ ] COMMENT（建议）

## 审查报告模板
```markdown
# Code Review Report (Puzzle71Solver)

**PR**: #<pr_number>
**Task**: T0XX
**Reviewer**: @<username>
**Date**: YYYY-MM-DD

## Summary
[总体评价]

## Constitution Compliance
- Determinism: ✓/✗
- Test-First: ✓/✗
- CPU/GPU Parity: ✓/✗
- Performance Threshold: ✓/✗
- Audit Trail: ✓/✗

## Findings

### Critical Issues (Must Fix)
- [ ] Issue 1: [描述]

### Major Issues (Should Fix)
- [ ] Issue 1: [描述]

### Minor Issues (Nice to Have)
- Issue 1: [描述]

## Performance Verification
- GPU Model: [型号]
- Baseline: XXX keys/sec
- Current: YYY keys/sec
- Ratio: Z.ZZ (threshold: 0.95)
- Register Usage: XX/128
- Occupancy: XX%

## Test Coverage
- Previous: XX.X%
- Current: YY.Y%
- Delta: +Z.Z%

## Determinism Check
- Replay verification: PASS/FAIL
- Config completeness: PASS/FAIL
- Non-deterministic APIs: NONE/FOUND

## Decision
- [x] APPROVE
- [ ] REQUEST_CHANGES
- [ ] COMMENT
```

```

---

## 10. 故障分级与熔断机制

### 10.1 故障分级

| 级别 | 定义 | 响应时间 | 处理流程 |
|------|------|---------|---------|
| P0 | 确定性破坏、CPU/GPU结果不一致、密钥泄露 | 立即 | 触发熔断+创建hotfix分支 |
| P1 | 性能严重退化（<50% baseline）、摘要验证失败 | 2小时 | 创建高优先级Issue |
| P2 | CI完全失败、测试大面积红 | 8小时 | 分配给Fixer Agent |
| P3 | 单个测试失败、性能警告 | 24小时 | 常规修复流程 |

### 10.2 熔断机制

```bash
#!/bin/bash
# ci/circuit_breaker.sh

FAILURE_HISTORY=".ci/failure_history.txt"
FAILURE_COUNT=$1

# 记录失败
echo "$(date +%Y-%m-%d) $FAILURE_COUNT" >> "$FAILURE_HISTORY"

# 获取最近3次的失败记录
RECENT_FAILURES=$(tail -n 3 "$FAILURE_HISTORY" | awk '{sum+=$2} END {print sum}')

echo "Recent failures (last 3 runs): $RECENT_FAILURES"

# 熔断阈值：连续3次失败
if [ "$RECENT_FAILURES" -ge 3 ]; then
    echo "=== CIRCUIT BREAKER TRIGGERED ===" | tee circuit_breaker.alert
    echo "Consecutive failures detected: $RECENT_FAILURES" | tee -a circuit_breaker.alert
    
    # 1. 锁定主分支
    echo "Locking main branch merges..." | tee -a circuit_breaker.alert
    gh api --method PUT \
        /repos/:owner/:repo/branches/main/protection \
        -f required_status_checks='{}' \
        -f enforce_admins=true \
        -f required_pull_request_reviews='{required_approving_review_count:2}'
    
    # 2. 创建紧急Issue
    gh issue create \
        --title "🚨 CIRCUIT BREAKER: Puzzle71Solver Build Failures" \
        --body "$(cat circuit_breaker.alert)" \
        --label "P0,circuit-breaker,puzzle71" \
        --assignee "@security-team"
    
    # 3. 检查是否是确定性破坏
    if grep -q "determinism" logs/ci_latest.log; then
        echo "CRITICAL: Determinism violation detected" | tee -a circuit_breaker.alert
        echo "  All GPU operations must be reproducible" | tee -a circuit_breaker.alert
        
        # 触发确定性事故处理
        ./ci/handle_determinism_incident.sh
    fi
    
    # 4. 检查是否是性能回归
    if grep -q "performance regression" logs/ci_latest.log; then
        echo "CRITICAL: Performance regression detected" | tee -a circuit_breaker.alert
        
        # 触发性能事故处理
        ./ci/handle_performance_incident.sh
    fi
    
    # 5. 发送告警
    ./tools/send_alert.sh "circuit_breaker" circuit_breaker.alert
    
    echo "Main branch is LOCKED until issue resolved." | tee -a circuit_breaker.alert
    exit 2
fi

echo "✓ Circuit breaker checks passed"
```

### 10.3 P0故障应急处理

```bash
#!/bin/bash
# ci/handle_determinism_incident.sh

TIMESTAMP=$(date +%Y%m%d_%H%M%S)
INCIDENT_ID="DET_${TIMESTAMP}"
INCIDENT_DIR="docs/incidents/${INCIDENT_ID}"

mkdir -p "$INCIDENT_DIR"

echo "=== P0 DETERMINISM INCIDENT ===" | tee "$INCIDENT_DIR/incident_report.md"
echo "Incident ID: $INCIDENT_ID" | tee -a "$INCIDENT_DIR/incident_report.md"
echo "Timestamp: $(date)" | tee -a "$INCIDENT_DIR/incident_report.md"
echo "" | tee -a "$INCIDENT_DIR/incident_report.md"

# 1. 收集诊断信息
echo "Step 1: Collecting diagnostics..." | tee -a "$INCIDENT_DIR/incident_report.md"

# Git历史
git log --oneline -20 > "$INCIDENT_DIR/recent_commits.txt"

# 最近的重放验证日志
if [ -d "telemetry/replay" ]; then
    cp -r telemetry/replay "$INCIDENT_DIR/"
fi

# 配置文件快照
cp config/puzzle71.yaml "$INCIDENT_DIR/"

# 最近的checkpoint
if [ -d "checkpoints" ]; then
    cp checkpoints/*.json "$INCIDENT_DIR/" 2>/dev/null || true
fi

# 2. 分析非确定性源
echo "" | tee -a "$INCIDENT_DIR/incident_report.md"
echo "Step 2: Analyzing non-deterministic sources..." | tee -a "$INCIDENT_DIR/incident_report.md"

# 扫描非确定性API
./ci/check_determinism.sh > "$INCIDENT_DIR/determinism_scan.log" 2>&1 || true

# 检查配置加载
if ! grep -q "replay_seed" config/puzzle71.yaml; then
    echo "ERROR: Missing replay_seed in config" | tee -a "$INCIDENT_DIR/incident_report.md"
fi

# 3. 创建hotfix分支
echo "" | tee -a "$INCIDENT_DIR/incident_report.md"
echo "Step 3: Creating hotfix branch..." | tee -a "$INCIDENT_DIR/incident_report.md"

HOTFIX_BRANCH="hotfix/${INCIDENT_ID}_determinism"
git checkout -b "$HOTFIX_BRANCH"

# 4. 启用严格确定性检查
echo "" | tee -a "$INCIDENT_DIR/incident_report.md"
echo "Step 4: Enabling strict determinism checks..." | tee -a "$INCIDENT_DIR/incident_report.md"

# 修改CMakeLists.txt启用运行时检查
cat >> CMakeLists.txt << EOF

# Emergency determinism checks (added by incident handler)
add_definitions(-DSTRICT_DETERMINISM_CHECKS=1)
add_definitions(-DFAIL_ON_NON_DETERMINISTIC_API=1)
EOF

# 5. 生成修复指引
cat > "$INCIDENT_DIR/fix_guidance.md" << EOF
# Determinism Incident Fix Guidance

## Incident ID
${INCIDENT_ID}

## Root Cause Analysis
[待填写]

## Common Non-Deterministic Sources
1. Hardware clocks: clock64(), clock()
2. System time: time(NULL), gettimeofday()
3. Uninitialized memory
4. Hardware RNG: curandGenerate*()
5. Dynamic kernel launch parameters
6. Thread scheduling dependencies

## Fix Checklist
- [ ] All RNG uses replay_seed from config
- [ ] All kernel launches use fixed grid/block from config
- [ ] No clock64()/time(NULL) calls
- [ ] All memory initialized before use
- [ ] Replay verification passes

## Verification
\`\`\`bash
# 运行重放验证
./scripts/replay/verify-replay.sh checkpoints/latest.json

# 多次运行应产生相同结果
for i in {1..5}; do
    ./Puzzle71Solver --replay-manifest checkpoints/latest.json \\
        --telemetry-jsonl telemetry/verify_\$i.jsonl
done

# 比对所有telemetry文件
diff telemetry/verify_1.jsonl telemetry/verify_2.jsonl
# 应该完全相同
\`\`\`

## Sign-off
- Fixed by: @
- Reviewed by: @
- Verified by: @
EOF

echo "" | tee -a "$INCIDENT_DIR/incident_report.md"
echo "Hotfix branch: $HOTFIX_BRANCH" | tee -a "$INCIDENT_DIR/incident_report.md"
echo "Fix guidance: $INCIDENT_DIR/fix_guidance.md" | tee -a "$INCIDENT_DIR/incident_report.md"
echo "" | tee -a "$INCIDENT_DIR/incident_report.md"
echo "CRITICAL: Main branch is LOCKED until determinism restored." | tee -a "$INCIDENT_DIR/incident_report.md"
```

---

## 11. AI Agent Prompt套件

### 11.1 Developer Agent标准Prompt

```markdown
# Puzzle71Solver Developer Agent Prompt

You are working on Puzzle71Solver, a CUDA-accelerated Bitcoin Puzzle #71 solver under STRICT deterministic and performance constraints.

## MANDATORY RULES (铁律 - 违反任何一条立即回滚)

1. **DETERMINISM-FIRST**: ALL GPU computations MUST be reproducible
   - Use replay_seed from config/puzzle71.yaml for ALL RNG
   - Use fixed grid/block dimensions from config
   - NO clock64(), time(NULL), std::random_device, or any non-deterministic APIs
   - Verify: Run scripts/replay/verify-replay.sh before committing

2. **TEST-FIRST-CUDA**: Write FAILING tests BEFORE implementation
   - Create test files in tests/unit/, tests/validation/, or tests/perf/
   - Run tests, confirm ALL fail (red light)
   - Save failure log to docs/validation/evidence/T0XX_test_failures.log
   - THEN implement code to make tests pass (green light)
   - Commit MUST reference test failure evidence

3. **NO-CRYPTO-REINVENTION**: Use reference sources ONLY
   - Verify @origin attribution exists BEFORE writing ECC code
   - Call secp256k1-zkp/VanitySearch/BitCrack through adapters in src/utils/
   - NEVER write your own: ec_mul, scalar_split, hash160, bigint operations
   - Mark ALL crypto code with @sot_ref comments

4. **ZERO-TOLERANCE-PERFORMANCE**: Meet baseline thresholds
   - RTX 2080 Ti: ≥1,000M keys/sec
   - RTX 3090: ≥2,000M keys/sec
   - A100: ≥4,000M keys/sec
   - Run scripts/run-benchmarks.sh before committing
   - CI WILL BLOCK if performance <95% of baseline

5. **MANDATORY-DIGEST**: ALL artifacts MUST have SHA-256 digests
   - Checkpoints, telemetry, benchmarks, reports
   - Use src/utils/digest_verifier.cpp functions
   - Verify digest computation SLA ≤250ms

## BEFORE YOU START

Answer these questions (ALL must be YES):

1. [ ] Have you verified extraction attribution? (ci/check_extraction_attribution.sh)
2. [ ] Have you checked config/puzzle71.yaml for deterministic parameters?
3. [ ] Have you written FAILING tests for this task?
4. [ ] Have you saved test failure evidence?
5. [ ] Are you using adapters to call reference code (NOT reimplementing)?
6. [ ] Does your code avoid clock64(), time(NULL), rand(), etc?

## CODE REQUIREMENTS

- Wrap ALL CUDA APIs with CUDA_CHECK() macro
- Add @reuse_check comments (5 levels) to ALL new functions
- Add @sot_ref comments referencing specific lines in reference sources
- Include complete Doxygen comments
- Use replay_seed from config for ALL randomness
- Use grid_dim/block_dim from config for ALL kernel launches
- Add SHA-256 digests to ALL output artifacts

## OUTPUT FORMAT

Provide your changes as:

1. **Task ID**: T0XX (from tasks.md)

2. **Test Failure Evidence**: Path to test failure log
   ```

   docs/validation/evidence/T029_test_failures.log

   ```

3. **Reuse Check Summary**:
   ```

- L1 (Current): Not implemented
- L2 (secp256k1-zkp): src/scalar_impl.h::secp256k1_scalar_split_lambda
- L3 (VanitySearch): GPU kernel pattern reference
- L4 (CUDA SDK): No direct API
- L5 (New): Justified - CUDA port of reference algorithm

   ```

4. **Determinism Verification**:

   ```

- RNG: Uses replay_seed from config ✓
- Kernel launch: Uses grid_dim/block_dim from config ✓
- No non-deterministic APIs ✓
- Replay test passes ✓

   ```

5. **Diff/Patch**:

   ```diff
   --- a/src/puzzle71_kernel.cu
   +++ b/src/puzzle71_kernel.cu
   @@ ...
   ```

6. **Test Cases**: Updated test files

   ```cpp
   TEST(BatchKernelTest, ProducesReproducibleResults) {
       // Test implementation...
   }
   ```

7. **Performance Impact**:

   ```
   Estimated throughput: 1200M keys/sec (RTX 2080 Ti)
   Baseline: 1000M keys/sec
   Ratio: 1.20x (above 0.95 threshold ✓)
   ```

## EXAMPLE RESPONSE

```markdown
### Task T029: Implement fused batch stepping kernel

#### Test Failure Evidence
Created failing tests:
- tests/unit/test_kernel_interfaces.cu
- tests/validation/test_hash160_gpu_cpu_parity.cpp  
- tests/perf/test_range_scan_benchmark.cu

Evidence saved to: docs/validation/evidence/T029_test_failures.log

Test results: 3/3 FAILED (as expected)

#### Reuse Check Summary
- L1: No existing batch kernel
- L2: VanitySearch/GPU/GPUEngine.cu L234-L456 (batch stepping pattern)
- L2: secp256k1-zkp/src/scalar_impl.h L789-L823 (endomorphism split)
- L3: Adapted VanitySearch memory layout, requires ~40% changes for HASH160 fusion
- L4: No CUDA SDK equivalent
- L5: New fused kernel justified - combines point multiplication + HASH160 in single launch

#### Determinism Verification
✓ RNG initialized from config.rng.base_seed
✓ Kernel launch uses config.kernel_launch.{grid_dim, block_dim}
✓ No clock64(), time(), or other non-deterministic APIs
✓ Replay seed recorded in checkpoint manifest

#### Implementation

\`\`\`diff
--- a/src/puzzle71_kernel.cu
+++ b/src/puzzle71_kernel.cu
@@ -0,0 +1,120 @@
+/**
+ * @brief Fused batch stepping + HASH160 kernel
+ * @reuse_check_L2 VanitySearch: GPU/GPUEngine.cu L234-L456
+ * @reuse_check_L2 secp256k1-zkp: src/scalar_impl.h L789-L823
+ * @sot_ref SOT-CRYPTO: VanitySearch/GPUEngine.cu (batch stepping)
+ * @sot_ref SOT-CRYPTO: secp256k1-zkp/scalar_impl.h (endomorphism)
+ */
+__global__ void puzzle71_batch_kernel(
+    const uint256_t* d_scalars,
+    uint160_t* d_hash_outputs,
+    size_t count,
+    uint64_t replay_seed
+) {
+    // Implementation...
+}
\`\`\`

#### Performance Verification
Benchmark results:
- Median: 1,150M keys/sec
- Min: 1,100M keys/sec
- Max: 1,200M keys/sec
- Baseline (2080 Ti): 1,000M keys/sec
- Ratio: 1.15x ✓

Register usage: 124/128 (within budget)

#### Test Results (After Implementation)
All tests now PASS:
- test_kernel_interfaces: PASS
- test_hash160_gpu_cpu_parity: PASS (0 mismatches / 1024 samples)
- test_range_scan_benchmark: PASS (1150M keys/sec ≥ 1000M threshold)
```

```

---

### 11.2 Fixer Agent专用Prompt

```markdown
# Puzzle71Solver Fixer Agent Prompt

You are a Fixer Agent responding to Puzzle71Solver CI failures. Your role is to analyze logs and propose MINIMAL fixes that restore determinism, performance, or correctness.

## CONSTRAINTS (不可妥协)

- Do NOT rewrite entire files
- Do NOT bypass tests (all tests must pass)
- Do NOT compromise determinism
- Fix root cause, not symptoms

## ANALYSIS PROCESS

1. **Classify Failure Type**:
   - Determinism violation (replay mismatch)
   - Performance regression (<95% baseline)
   - CPU/GPU parity failure (mismatched results)
   - Test failure (assertion failed)
   - Build failure (compilation error)

2. **Root Cause Analysis**:
   - Quote exact error message
   - Identify failing code location
   - Explain why it failed
   - Check if determinism was broken

3. **Minimal Fix Strategy**:
   - What's the smallest change to fix this?
   - Can we adjust config instead of code?
   - Is there a reference implementation we missed?

4. **Verification Plan**:
   - Which tests confirm the fix?
   - How to verify determinism restored?
   - Expected performance after fix?

## OUTPUT FORMAT

```markdown
# Fix Analysis Report

## Failure Classification
[Determinism Violation | Performance Regression | CPU/GPU Parity Failure | Test Failure | Build Failure]

## Root Cause
\`\`\`
[Exact error message from logs]
\`\`\`

**Location**: [file:line]

**Explanation**: [Why it failed]

**Determinism Impact**: [YES/NO - does this affect reproducibility?]

## Minimal Fix
\`\`\`diff
[Exact patch - smallest possible change]
\`\`\`

## Rationale
[Why this is the minimal necessary change]

## Verification

### Tests to Run
\`\`\`bash
# Unit tests
ctest -R test_xxx

# Determinism verification
./scripts/replay/verify-replay.sh checkpoints/latest.json

# Performance verification
./scripts/run-benchmarks.sh 0 5
\`\`\`

### Expected Results
- All tests: PASS
- Replay verification: IDENTICAL telemetry
- Performance: ≥XXX keys/sec (baseline)

## Risk Assessment
- Low / Medium / High
- Affected modules: [list]
- Determinism preserved: YES/NO
\`\`\`

## EXAMPLE

Given CI failure:
\`\`\`
ERROR: test_gpu_determinism.cu:67
  Replay telemetry mismatch at line 42
  Expected keys_per_sec: 1000123456
  Actual keys_per_sec:   1000234567
\`\`\`

Your response:

```markdown
# Fix Analysis Report

## Failure Classification
Determinism Violation

## Root Cause
\`\`\`
ERROR: test_gpu_determinism.cu:67
  Replay telemetry mismatch at line 42
  Expected keys_per_sec: 1000123456
  Actual keys_per_sec:   1000234567
\`\`\`

**Location**: src/puzzle71_kernel.cu:123

**Explanation**: The kernel is using threadIdx.x directly as an RNG seed instead of deriving from replay_seed. This causes different GPU thread scheduling to produce different results.

**Determinism Impact**: YES - breaks reproducibility

## Minimal Fix
\`\`\`diff
--- a/src/puzzle71_kernel.cu
+++ b/src/puzzle71_kernel.cu
@@ -120,7 +120,7 @@
 __global__ void puzzle71_batch_kernel(...) {
     int tid = blockIdx.x * blockDim.x + threadIdx.x;
     
-    uint64_t rng_seed = threadIdx.x;  // ❌ Non-deterministic
+    uint64_t rng_seed = replay_seed + tid;  // ✓ Deterministic
     
     xorshift64_state rng = init_rng(rng_seed);
     // ...
\`\`\`

## Rationale
Single line change: derive RNG seed from the provided replay_seed parameter instead of hardware thread ID. This ensures identical results across runs.

## Verification

### Tests to Run
\`\`\`bash
# Determinism test
ctest -R test_gpu_determinism

# Replay verification  
./scripts/replay/verify-replay.sh checkpoints/test_checkpoint.json

# Full test suite
ctest --output-on-failure
\`\`\`

### Expected Results
- test_gpu_determinism: PASS
- Replay telemetry: IDENTICAL (zero diff)
- All other tests: PASS (no regressions)

## Risk Assessment
- Low
- Affected modules: puzzle71_kernel.cu only
- Determinism preserved: YES (this fix RESTORES determinism)
```

```

---

## 12. 工具脚本索引与执行频率

| 脚本路径 | 用途 | 执行频率 | 强制性 |
|---------|------|---------|--------|
| `ci/check_extraction_attribution.sh` | 验证代码提取溯源 | 每次CI | 强制 |
| `ci/check_determinism.sh` | 检测非确定性API | 每次CI | 强制 |
| `ci/tdd-gate.yml` | TDD证据验证 | 每次CI | 强制 |
| `ci/check_crypto_reinvention.sh` | 检测密码学重新实现 | 每次CI | 强制 |
| `ci/performance_gate.sh` | 性能基线门禁 | 每次CI | 强制 |
| `ci/verify_all_digests.sh` | 验证所有摘要 | 每次CI | 强制 |
| `ci/check_new_files.sh` | 新文件白名单检查 | 每次CI | 强制 |
| `ci/scan_placeholders.sh` | 占位符扫描 | 每次CI | 强制 |
| `ci/verify_license_files.sh` | 许可证文件检查 | 每次CI | 强制 |
| `scripts/replay/verify-replay.sh` | 确定性重放验证 | 提交前+CI | 强制 |
| `scripts/run-benchmarks.sh` | GPU性能基准测试 | 提交前+CI+每晚 | 强制 |
| `tools/nsight/check_register_budget.sh` | 寄存器预算检查 | CI+按需 | 强制 |
| `scripts/digest/check-artifact-digests.sh` | 摘要完整性扫描 | CI+每晚 | 强制 |
| `scripts/generate-report.sh` | 生成运行报告 | 程序退出时 | 自动 |
| `scripts/run-qa.sh` | QA流水线 | 程序退出时+每晚 | 自动 |
| `scripts/purge-checkpoints.sh` | 清理过期checkpoint | 每晚 | 自动 |
| `ci/nightly_build.sh` | 夜间完整构建 | 每晚 | 自动 |
| `ci/circuit_breaker.sh` | 熔断机制 | 每晚 | 自动 |
| `ci/handle_determinism_incident.sh` | 确定性事故处理 | 故障时 | 自动 |
| `ci/handle_performance_incident.sh` | 性能事故处理 | 故障时 | 自动 |

---

## 附录A：快速参考卡

### 开发者速查

```bash
# === 工作前准备 ===
./ci/check_extraction_attribution.sh
./ci/scan_placeholders.sh

# === TDD工作流 ===
# 1. 编写失败的测试
vim tests/unit/test_xxx.cpp
mkdir -p build && cd build && cmake .. && make && cd ..
./build/tests/test_xxx 2>&1 | tee docs/validation/evidence/T0XX_test_failures.log

# 2. 编写实现
vim src/xxx.cpp

# 3. 验证测试通过
./build/tests/test_xxx

# === 本地验证（提交前必做） ===
./ci/check_determinism.sh
./ci/check_crypto_reinvention.sh
./ci/scan_placeholders.sh
./scripts/replay/verify-replay.sh checkpoints/latest.json
./scripts/run-benchmarks.sh 0 5
./tools/nsight/check_register_budget.sh

# === 提交 ===
git add <files>
git commit -m "T0XX: <description>

Test evidence: docs/validation/evidence/T0XX_test_failures.log
Performance: XXX keys/sec (YY% above baseline)
Determinism: Verified with replay test"

git push origin <branch>
```

### 审查者速查

```bash
# === 检查PR ===
gh pr checkout <pr_number>

# === 验证TDD ===
ls docs/validation/evidence/T0XX_test_failures.log
git log --follow docs/validation/evidence/T0XX_test_failures.log
git log --follow src/xxx.cpp
# 验证evidence时间戳早于implementation

# === 运行检查 ===
./ci/check_determinism.sh
./ci/check_crypto_reinvention.sh
./ci/check_extraction_attribution.sh
./ci/verify_license_files.sh

# === 运行测试 ===
mkdir build && cd build && cmake .. && make && cd ..
./build/tests/test_all

# === 性能验证 ===
./scripts/run-benchmarks.sh 0 5
./ci/performance_gate.sh

# === 确定性验证 ===
./scripts/replay/verify-replay.sh checkpoints/latest.json
```

---

## 附录B：宪法原则快速对照表

| 宪法原则 | 本规范章节 | CI门禁 | 关键检查点 |
|---------|-----------|--------|-----------|
| **Principle I<br>确定性密码学与可重现性** | 第3节<br>确定性重放约束 | `ci/check_determinism.sh`<br>`scripts/replay/verify-replay.sh` | • 所有RNG使用replay_seed<br>• Kernel使用config参数<br>• CPU/GPU一致性<br>• Replay零差异 |
| **Principle II<br>测试优先GPU安全** | 第5节<br>测试驱动开发工作流 | `ci/tdd-gate.yml`<br>`ctest` | • 测试先失败后通过<br>• 失败证据完整<br>• 覆盖率≥80%<br>• GPU vs CPU parity |
| **Principle III<br>可观测性与性能预算** | 第4节<br>性能门槛强制执行 | `ci/performance_gate.sh`<br>`tools/nsight/check_register_budget.sh` | • 吞吐量≥baseline<br>• 寄存器≤128/thread<br>• 占用率≥85%<br>• Telemetry完整 |
| **Principle IV<br>密钥安全与伦理** | 第7节<br>防篡改与审计追踪 | `ci/verify_all_digests.sh`<br>审计日志检查 | • 所有artifact有摘要<br>• 操作员ID记录<br>• 审计日志WORM<br>• 密钥零化 |
| **Principle V<br>文档与知识转移** | 第9节<br>文档自动化更新 | `scripts/generate-report.sh`<br>`scripts/run-qa.sh` | • Quickstart更新<br>• 性能报告生成<br>• 证据归档<br>• 合规检查表 |

---

## 文档版本与变更记录

| 版本 | 日期 | 主要变更 | 作者 |
|------|------|---------|------|
| v5.0 | 2025-09-30 | **Puzzle71Solver铁笼协议初始版** | Constraint Team |
|      |            | - 针对CUDA密码学项目定制 | |
|      |            | - 新增DETERMINISM-FIRST原则 | |
|      |            | - 新增TEST-FIRST-CUDA工作流 | |
|      |            | - 新增NO-CRYPTO-REINVENTION强制溯源 | |
|      |            | - 新增ZERO-TOLERANCE-PERFORMANCE门槛 | |
|      |            | - 新增MANDATORY-DIGEST防篡改 | |
|      |            | - 专项确定性重放约束（第3节） | |
|      |            | - 性能基线强制执行（第4节） | |
|      |            | - TDD证据验证门禁 | |
|      |            | - 引用源适配器模式 | |
|      |            | - 操作员审计追踪 | |
|      |            | - CUDA寄存器预算检查 | |
|      |            | - 摘要性能SLA（≤250ms） | |
|      |            | - 完整CI流水线（8阶段） | |
|      |            | - 确定性事故处理流程 | |
|      |            | - 性能事故处理流程 | |
|      |            | - 熔断机制（连续3次失败） | |
|      |            | - Developer/Reviewer检查表 | |
|      |            | - AI Agent Prompt套件 | |

---

**文档结束。所有规则强制执行，违反任何一条导致立即回滚。**

**CRITICAL**: 本规范是Puzzle71Solver项目宪法的可执行实现。任何与宪法原则冲突的代码变更必须在设计阶段被拒绝，任何绕过本规范的尝试将触发熔断并锁定主分支。

**记住**：确定性、性能和安全是三大支柱，缺一不可。测试优先是守护这三大支柱的基石。参考源复用是避免重复劳动和引入缺陷的关键。所有规则的背后都是为了保证最终产物的正确性、可验证性和可维护性。

# Puzzle71Solver

Puzzle71Solver 是一个针对比特币 Puzzle #71 的 GPU 批量密钥搜索引擎，使用 C++17 + CUDA 11.8 构建，集成 BitCrack / CudaBrainSecp / secp256k1-zkp 等参考实现。在最新迭代中我们完善了确定性配置、重放种子、基准测试与 CI 门禁，可复现 GPU/CPU parity、重放证据并输出完整的操作审计链路。

## 环境要求

| 组件 | 版本/说明 |
|------|-----------|
| 操作系统 | Ubuntu 22.04 (WSL2 亦可，但性能与寄存器计数器受限) |
| GPU 驱动 | NVIDIA Driver ≥ 535，CUDA Toolkit 11.8 |
| 编译器 | GCC ≥ 10、CMake ≥ 3.18 |
| 依赖库 | OpenSSL、GoogleTest（自动通过 FetchContent 获取） |
| 可选工具 | `nsight-cu-cli` (原生 Linux 下做性能分析)、`cuobjdump`、`nvidia-smi` |

> **提示**：WSL2 默认禁用 GPU 性能计数器（ERR_NVGPUCTRPERM）。寄存器预算可以通过 `tools/static_analysis/check_register_usage.sh` 静态获取，如需完整 Nsight profiling，请在原生 Linux 主机启用权限。

## 安装与构建

```bash
# 获取代码
git clone https://github.com/<your-org>/Puzzle71Solver.git
cd Puzzle71Solver

# 配置并编译 (Release)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# 运行单元/集成测试
cd build
ctest -R puzzle71_tests --output-on-failure
```

首次运行可执行 `./scripts/run-qa.sh --mode smoke` 验证基本流水线；完整 QA（含基准、摘要校验、报告生成）直接运行 `./scripts/run-qa.sh`。

## 配置 (config/puzzle71.yaml)

```yaml
project_constants:
  puzzle:
    target_address: "1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU"
    hash160: "f8455b22fa469a40654450d363959a3b932924b4"
    keyspace:
      start: "0x400000000000000000"
      end: "0x7fffffffffffffffff"

operator_defaults:
  operator_id: "unset"
  operator_purpose: "research"

replay:
  grid_dim: [4096, 1, 1]
  block_dim: [256, 1, 1]
  points_per_thread: 4096
  deterministic_seed: 424242
```

- `replay` 段定义了确定性的 kernel 启动参数与 RNG 种子；当传入 `--resume-manifest` 或默认扫描时，Solver 会优先使用这些配置信息。
- 可修改 `operator_defaults` 以免频繁在命令行重复输入。

## 命令行使用

### 必选参数

| 参数 | 说明 |
|------|------|
| `--keyspace <start:end>` | 16 进制闭区间，必须属于 Puzzle #71 的授权范围 (0x4000…–0x7fff…) |
| `--target-address <addr>` | 必须等于 Puzzle #71 地址 `1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU`（除 parity 测试外） |
| `--operator-id <id>` | 操作员标识，写入 checkpoint/telemetry/luck.txt |
| `--operator-purpose <purpose>` | 操作目的描述，用于审计 |

### 常用可选参数

| 参数 | 说明 |
|------|------|
| `--device <ids>` | 指定使用的 CUDA 设备（如 `--device 0,1`）；默认使用全部可用 GPU |
| `--dry-run` | 只做配置校验，不启动 kernel |
| `--enable-checkpoint` | 开启 AES-256-GCM checkpoint（输出到 `checkpoints/`） |
| `--telemetry-jsonl <dir>` | 存放 NDJSON telemetry 的目录（默认 `telemetry/`） |
| `--prometheus-export <dir>` | 输出 Prometheus textfile 指标（默认关闭） |
| `--replay-manifest <file>` | 读取 checkpoint manifest，执行一致性重放 |
| `--resume-manifest <file>` | 从 manifest 中恢复 `next_scalar` 与 batch 配置，继续未完成的扫描 |
| `--parity-test-scalar <hex>` | 覆写目标 HASH160，用于 GPU/CPU parity 采样 |
| `--luck-file <path>` | 匹配密钥写入路径（默认 `luck.txt`） |

### 运行示例

1. **Dry run 校验配置**
   ```bash
   ./build/Puzzle71Solver \
     --keyspace 0x400000000000000000:0x4000000000000000FF \
     --target-address 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU \
     --operator-id ops01 \
     --operator-purpose validation \
     --dry-run
   ```

2. **实际扫描（单 GPU）**
   ```bash
   ./build/Puzzle71Solver \
     --keyspace 0x400000000000000000:0x40000000000FFFFFF \
     --target-address 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU \
     --operator-id prod01 \
     --operator-purpose main-scan \
     --device 0 \
     --telemetry-jsonl telemetry/run_$(date +%Y%m%d%H%M%S) \
     --enable-checkpoint
   ```

3. **GPU/CPU parity 采样**
   ```bash
   ./build/Puzzle71Solver \
     --keyspace 0xa00:0xbff \
     --target-address 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU \
     --parity-test-scalar 0xabc \
     --operator-id parity \
     --operator-purpose doc \
     --device 0 \
     --telemetry-jsonl docs/validation/evidence/2025-10-01-parity \
     --enable-checkpoint \
     --luck-file docs/validation/evidence/2025-10-01-parity/luck_parity_run.txt
   ```

详细命令组合可参考 `docs/validation/evidence/2025-10-01-parity/` 中的日志归档。

## 基准与性能门禁

- 自动化脚本：`scripts/run-benchmarks.sh`
  - 支持预热/测量、吞吐统计、基线对比，输出 `benchmarks/latest.json`。
  - WSL2 基线存放于 `benchmarks/baseline/gpu_baselines_wsl2.json`（目前数据：RTX 2080 Ti ≥ 50M keys/sec）。
- CI 门禁：
  - `ci/performance_gate.sh` 调用基准脚本并打印统计信息；若低于基线 95% 则退出 1。
  - `ci/determinism_gate.sh` 连续运行两次相同 keyspace 并对比 telemetry（忽略 `timestamp/elapsed_ms/keys_per_sec`）。

示例：
```bash
# 性能门禁
ci/performance_gate.sh
# 确定性门禁
ci/determinism_gate.sh
```

## 证据与审核

| 位置 | 内容 |
|------|------|
| `docs/validation/evidence/2025-10-01-parity/` | 最新 parity 运行（telemetry / manifest / payload / luck / 命令日志） |
| `docs/validation/evidence/nsight/` | 静态寄存器分析报告、WSL2 Nsight 替代方案说明 |
| `docs/validation/evidence/tdd/` | T029–T041 预实现失败日志归档 |

## 后续工作

- 在原生 Linux 环境运行 Nsight Compute 收集实时指标（IPC / 带宽 / cache）。
- 根据 `docs/workarounds/nsight_alternatives_wsl2.md` 的计划完成代码重构与多 GPU 长时测试。
- 在 `docs/performance.md`、`docs/runbooks/checkpoint.md` 等文档中持续同步最新基准与操作流程。

## 支持与反馈

如在构建或运行过程中遇到问题，请记录命令输出与相关日志（telemetry、checkpoint manifest），并在提交 Issue 时附上 `benchmarks/latest.json` 与 `docs/validation/evidence` 中的证据文件，便于复现与审计。

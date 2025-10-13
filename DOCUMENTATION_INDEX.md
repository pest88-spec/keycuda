# 📚 Puzzle71Solver 文档索引

**项目**: Puzzle71Solver - GPU加速比特币私钥搜索引擎
**版本**: v0.3.0 (GPU性能优化完成)
**最后更新**: 2025-10-12

---

## 🎯 快速开始

| 文档 | 用途 | 路径 |
|------|------|------|
| **项目README** | 快速入门、安装部署、使用指南 | [`README.md`](README.md) |
| **AI开发指南** | Claude Code开发规范和工作流 | [`CLAUDE.md`](CLAUDE.md) |
| **文档验证脚本** | 一键验证所有文档完整性 | [`scripts/verify-documentation.sh`](scripts/verify-documentation.sh) |

**快速验证**:
```bash
./scripts/verify-documentation.sh
```

---

## 📋 核心规范文档（Speckit工具链）

### GPU性能优化特性 (003-gpu-1-28)

本项目使用 **Speckit 规范化开发工具链** 管理需求、计划和任务。

| 文档类型 | 文件路径 | 内容说明 | 大小 |
|---------|---------|---------|------|
| **特性规范** | [`specs/003-gpu-1-28/spec.md`](specs/003-gpu-1-28/spec.md) | 需求规格：3个用户故事、12个功能需求(FR)、6个非功能需求(NFR)、8个成功标准(SC) | 16.7 KB |
| **实施计划** | [`specs/003-gpu-1-28/plan.md`](specs/003-gpu-1-28/plan.md) | 技术栈(CUDA C++20, Thrust/CUB)、架构设计、文件结构规划、7个实施阶段 | 16.4 KB |
| **任务列表** | [`specs/003-gpu-1-28/tasks.md`](specs/003-gpu-1-28/tasks.md) | 57个任务(T001-T057)，分为7个阶段，100%完成 | 35.3 KB |
| **数据模型** | [`specs/003-gpu-1-28/data-model.md`](specs/003-gpu-1-28/data-model.md) | 8个核心实体定义：GPUKernelConfiguration, PerformanceBaseline, BenchmarkResult等 | 27.2 KB |
| **技术研究** | [`specs/003-gpu-1-28/research.md`](specs/003-gpu-1-28/research.md) | CUDA最佳实践、Thrust/CUB集成、共享内存优化、Warp级原语、测试策略 | 39.2 KB |
| **技术债务** | [`specs/003-gpu-1-28/technical_debt_register.md`](specs/003-gpu-1-28/technical_debt_register.md) | 技术债务清单(215→12项，94%减少)、并行化机会分析 | 17.1 KB |

### 项目宪法 (铁笼协议v5.0)

| 文档 | 路径 | 内容说明 |
|------|------|---------|
| **项目宪法** | [`.specify/memory/constitution.md`](.specify/memory/constitution.md) | 7条开发原则：确定性计算、测试优先、密码学安全、零回归、强制摘要、科学验证、内存层级优化 | 16.5 KB |

---

## 🔧 API契约规范

| 契约文档 | 路径 | 内容说明 |
|---------|------|---------|
| **CUDA内核API** | [`specs/003-gpu-1-28/contracts/kernel-api.md`](specs/003-gpu-1-28/contracts/kernel-api.md) | ECC标量乘法、共享内存优化、Warp级原语的函数签名和测试要求 |
| **基准测试API** | [`specs/003-gpu-1-28/contracts/benchmarking-api.md`](specs/003-gpu-1-28/contracts/benchmarking-api.md) | 性能基准管理、基准对比、遥测采集的接口规范 |

---

## 📊 质量检查清单

| 检查清单 | 路径 | 内容说明 |
|---------|------|---------|
| **需求清单** | [`specs/003-gpu-1-28/checklists/requirements.md`](specs/003-gpu-1-28/checklists/requirements.md) | 规范质量验证清单(16/16检查通过，100%质量评分) |

---

## 🚀 技术文档

### GPU优化专题

| 文档 | 路径 | 内容说明 | 大小 |
|------|------|---------|------|
| **GPU优化指南** | [`docs/GPU_OPTIMIZATION_GUIDE.md`](docs/GPU_OPTIMIZATION_GUIDE.md) | 共享内存优化、SoA布局、Warp级原语、并行算法的实施细节 | 9.4 KB |
| **代码溯源文档** | [`docs/reference-sources.md`](docs/reference-sources.md) | 提取的第三方代码溯源信息(BitCrack 33文件、secp256k1-zkp 58文件) | 8.0 KB |

### 性能基准测试

| 文档 | 路径 | 内容说明 |
|------|------|---------|
| **基准测试指南** | [`docs/benchmarks/README.md`](docs/benchmarks/README.md) | 基准测试解读、Nsight Compute分析、CI性能门禁故障排查 |
| **RTX 2080 Ti基准** | [`benchmarks/baselines/rtx2080ti.json`](benchmarks/baselines/rtx2080ti.json) | 基准吞吐量: 1.0 Gkeys/s，SHA-256保护 |
| **RTX 3090基准** | [`benchmarks/baselines/rtx3090.json`](benchmarks/baselines/rtx3090.json) | 基准吞吐量: 2.0 Gkeys/s，SHA-256保护 |
| **H20基准** | [`benchmarks/baselines/h20.json`](benchmarks/baselines/h20.json) | 基准吞吐量: 3.5 Gkeys/s，SHA-256保护 |
| **A100基准** | [`benchmarks/baselines/a100.json`](benchmarks/baselines/a100.json) | 基准吞吐量: 4.0 Gkeys/s，SHA-256保护 |

### 审计报告

| 报告 | 路径 | 日期 | 内容说明 |
|------|------|------|---------|
| **架构审计报告** | [`docs/reviews/architecture-audit-2025-10-12.md`](docs/reviews/architecture-audit-2025-10-12.md) | 2025-10-12 | 架构质量评估、技术债务分析 |
| **审计总结** | [`docs/reviews/audit-summary-2025-10-12.md`](docs/reviews/audit-summary-2025-10-12.md) | 2025-10-12 | 综合审计总结、改进建议 |
| **项目审计报告** | [`audits/PROJECT_AUDIT_REPORT_2025.md`](audits/PROJECT_AUDIT_REPORT_2025.md) | 2024-12-28 | 外部审计报告(严格标准，2.4/10评分) |
| **规范分析审计** | [`docs/reviews/specification-analysis-audit-2025-10-12.md`](docs/reviews/specification-analysis-audit-2025-10-12.md) | 2025-10-12 | 规范文档一致性审计(2.5/10评分，后被验证为误判) |

---

## 📖 许可证与合规

| 文档 | 路径 | 内容说明 |
|------|------|---------|
| **BitCrack许可证** | [`docs/licenses/BitCrack-LICENSE.MIT`](docs/licenses/BitCrack-LICENSE.MIT) | BitCrack项目MIT许可证原文 |

---

## 🗂️ 文档组织结构

### 目录树状图

```
PuzzleKeyhunt/
├── README.md                           # 项目主README
├── CLAUDE.md                           # AI开发指南
├── DOCUMENTATION_INDEX.md              # 本文档索引
│
├── specs/                              # 特性规范目录
│   └── 003-gpu-1-28/                   # GPU性能优化特性
│       ├── spec.md                     # 特性规范(需求)
│       ├── plan.md                     # 实施计划(技术栈+架构)
│       ├── tasks.md                    # 任务列表(57个任务)
│       ├── data-model.md               # 数据模型(8个实体)
│       ├── research.md                 # 技术研究文档
│       ├── technical_debt_register.md  # 技术债务登记
│       ├── checklists/                 # 质量检查清单
│       │   └── requirements.md
│       └── contracts/                  # API契约规范
│           ├── kernel-api.md
│           └── benchmarking-api.md
│
├── .specify/                           # Speckit工具目录
│   ├── memory/
│   │   └── constitution.md             # 项目宪法(7条原则)
│   └── scripts/
│       └── bash/
│           └── check-prerequisites.sh  # 文档验证脚本
│
├── docs/                               # 项目文档目录
│   ├── GPU_OPTIMIZATION_GUIDE.md       # GPU优化指南
│   ├── reference-sources.md            # 代码溯源文档
│   ├── benchmarks/
│   │   └── README.md                   # 基准测试指南
│   ├── licenses/
│   │   └── BitCrack-LICENSE.MIT        # 第三方许可证
│   └── reviews/                        # 审计报告目录
│       ├── architecture-audit-2025-10-12.md
│       ├── audit-summary-2025-10-12.md
│       └── specification-analysis-audit-2025-10-12.md
│
├── benchmarks/                         # 性能基准目录
│   └── baselines/                      # GPU基准文件
│       ├── rtx2080ti.json              # RTX 2080 Ti基准
│       ├── rtx3090.json                # RTX 3090基准
│       ├── h20.json                    # H20基准
│       └── a100.json                   # A100基准
│
├── scripts/                            # 工具脚本目录
│   └── verify-documentation.sh         # 文档完整性验证脚本
│
└── audits/                             # 外部审计目录
    └── PROJECT_AUDIT_REPORT_2025.md    # 项目审计报告
```

---

## 📝 文档约定

### 命名规范

- **特性分支**: `00X-feature-name` (例如: `003-gpu-1-28`)
- **任务编号**: `T001` ~ `T0XX` (例如: `T001`, `T057`)
- **需求编号**: `FR-001` ~ `FR-XXX` (功能需求), `NFR-001` ~ `NFR-XXX` (非功能需求)
- **成功标准**: `SC-001` ~ `SC-XXX` (例如: `SC-001`)
- **用户故事**: `User Story 1/2/3` 或 `US1/US2/US3`

### 文件大小参考

| 文档类型 | 典型大小 | 行数范围 |
|---------|---------|---------|
| **spec.md** | 15-20 KB | 100-150 行 |
| **plan.md** | 15-20 KB | 250-300 行 |
| **tasks.md** | 30-40 KB | 500-600 行 |
| **data-model.md** | 25-30 KB | 500-600 行 |
| **research.md** | 35-45 KB | - |

---

## 🔍 文档搜索指南

### 按主题查找

| 主题 | 关键文档 |
|------|---------|
| **性能优化** | GPU_OPTIMIZATION_GUIDE.md, plan.md (Phase 3-4), tasks.md (T011-T025) |
| **技术债务** | technical_debt_register.md, tasks.md (T026-T036) |
| **测试与验证** | plan.md (Testing), tasks.md (test tasks), contracts/ |
| **性能基准** | benchmarks/baselines/, docs/benchmarks/README.md |
| **CI/CD** | plan.md (CI Integration), tasks.md (T037-T048) |
| **代码溯源** | reference-sources.md, docs/licenses/ |

### 按开发阶段查找

| 阶段 | 关键文档 |
|------|---------|
| **需求分析** | spec.md (需求和用户故事) |
| **架构设计** | plan.md (技术栈和架构), data-model.md |
| **实施开发** | tasks.md (任务列表), contracts/ (API契约) |
| **质量保证** | checklists/, docs/reviews/ (审计报告) |
| **性能测试** | benchmarks/, docs/benchmarks/ |
| **部署运维** | README.md (部署指南) |

---

## 🛠️ 常用命令

### 文档验证

```bash
# 验证所有文档完整性
./scripts/verify-documentation.sh

# 检查特性目录结构
ls -la specs/003-gpu-1-28/

# 检查Constitution文件
cat .specify/memory/constitution.md

# 统计任务完成度
grep -c "^- \[X\] T0" specs/003-gpu-1-28/tasks.md
```

### 文档搜索

```bash
# 搜索所有spec.md文件
find . -name "spec.md"

# 搜索任务编号
grep -rn "T001" specs/

# 搜索需求编号
grep -rn "FR-001" specs/

# 搜索用户故事
grep -rn "User Story" specs/
```

---

## 📞 支持与反馈

### 文档问题报告

如发现文档问题，请提供：
1. 文档路径和位置
2. 问题描述（缺失、错误、过时）
3. 建议的修正方案

### 文档改进建议

欢迎提交Pull Request改进文档：
- 补充缺失的章节
- 修正错误信息
- 更新过时内容
- 增加示例和图表

---

**文档索引版本**: 1.0.0
**最后更新**: 2025-10-12
**维护者**: PuzzleKeyhunt Team
**验证状态**: ✅ 所有核心文档完整

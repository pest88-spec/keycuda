# P0-C003 模块体系统一计划

## 📋 任务概述

**任务ID**: P0-C003
**优先级**: P0 - Critical（立即修复）
**工作量**: 40小时
**目标**: 统一模块体系，废弃KeyhuntCore，解决3个并行模块体系的职责重叠问题

---

## 🔍 问题分析

### 当前模块体系混乱

项目中存在3个并行的模块体系，职责重叠严重：

#### 1. KeyhuntCore/ (计划废弃)
- **状态**: 仅在测试中被引用，主程序未使用
- **内容**: 
  - `benchmarks/` - 基准测试管理
  - `compare/` - Hash并行计算
  - `gpu/` - GPU执行器、内存管理、自适应批处理
  - `kernels/` - ECC标量乘法kernel
  - `scan/` - 优化扫描器
  - `utils/` - CUDA工具、文件I/O、JSON序列化
  - `validation/` - 一致性检查器

**问题**: 与ComputeCore功能重复，未被主程序使用

#### 2. ComputeCore/ (实际使用)
- **状态**: 主程序实际使用的模块
- **内容**:
  - `adapters/` - 适配器层（BatchInverseAdapter等）
  - `gpu/` - GPU执行器、设备结果、批处理计划
  - `shards/` - 分片遍历器

**问题**: 与KeyhuntCore功能重复

#### 3. Core/ECC/ (优化层)
- **状态**: ECC算法优化层
- **内容**: GLV Endomorphism、Batch Inverse等优化算法

**问题**: 与KeyhuntCore/gpu和ComputeCore/gpu职责不清

---

## 🎯 统一方案

### 目标架构

```
src/
├── core/                    # 核心算法层
│   ├── ecc/                # ECC算法（保留Core/ECC）
│   │   ├── glv_endomorphism.cpp
│   │   ├── batch_inverse.cpp
│   │   └── secp256k1_adapter.cpp
│   └── uint256.cpp         # 大整数运算
│
├── compute/                # 计算执行层（统一ComputeCore）
│   ├── gpu/               # GPU执行
│   │   ├── executor.cpp   # GPU执行器
│   │   ├── device_results.h
│   │   ├── batch_planner.cpp
│   │   └── memory_manager.cpp
│   ├── adapters/          # 适配器层
│   │   ├── batch_inverse_adapter.cpp
│   │   └── glv_adapter.cpp
│   └── shards/            # 分片管理
│       └── shard_walker.cpp
│
├── kernels/               # CUDA内核层（新增）
│   ├── ecc_kernel.cu      # ECC专用kernel
│   ├── hash_kernel.cu     # Hash专用kernel
│   └── puzzle71_kernel.cu # 原始kernel（逐步废弃）
│
├── utils/                 # 工具层（统一）
│   ├── telemetry_logger.cpp
│   ├── checkpoint_crypto.cpp
│   ├── digest_verifier.cpp
│   └── prometheus_exporter.cpp
│
├── integration/           # 集成层（保留）
│   ├── audit_logger.cpp
│   ├── manifest.cpp
│   └── metrics.cpp
│
├── config/                # 配置层（保留）
│   └── puzzle71_config.cpp
│
└── main.cpp              # 主程序
```

### 迁移策略

#### 阶段1: 分析依赖关系（2小时）
1. 分析KeyhuntCore中哪些文件被使用
2. 分析ComputeCore的依赖关系
3. 识别重复代码和功能

#### 阶段2: 迁移有用代码（16小时）
1. **迁移benchmarks/** → `integration/benchmarks/`
   - baseline_manager.cpp
   - benchmark_runner.cpp
   - telemetry_collector.cpp

2. **迁移utils/** → `utils/`
   - file_io.cpp → utils/file_io.cpp
   - json_serializer.cpp → utils/json_serializer.cpp

3. **废弃重复代码**:
   - KeyhuntCore/gpu/* (与ComputeCore/gpu重复)
   - KeyhuntCore/kernels/* (已有src/kernels/)
   - KeyhuntCore/compare/* (已有src/compare/)

#### 阶段3: 重命名ComputeCore（8小时）
1. 将`src/ComputeCore/`重命名为`src/compute/`
2. 更新所有引用路径
3. 更新CMakeLists.txt

#### 阶段4: 清理KeyhuntCore（4小时）
1. 删除未使用的KeyhuntCore文件
2. 更新测试引用路径
3. 更新文档

#### 阶段5: 验证和测试（10小时）
1. 编译测试
2. 运行单元测试
3. 运行集成测试
4. 性能基准测试

---

## 📝 详细执行步骤

### Step 1: 创建新目录结构
```bash
mkdir -p src/core/ecc
mkdir -p src/compute/gpu
mkdir -p src/compute/adapters
mkdir -p src/compute/shards
mkdir -p src/integration/benchmarks
```

### Step 2: 迁移文件
```bash
# 迁移benchmarks
mv src/KeyhuntCore/benchmarks/* src/integration/benchmarks/

# 迁移utils
mv src/KeyhuntCore/utils/file_io.* src/utils/
mv src/KeyhuntCore/utils/json_serializer.* src/utils/

# 重命名ComputeCore
mv src/ComputeCore src/compute
```

### Step 3: 更新CMakeLists.txt
- 移除KeyhuntCore引用
- 更新ComputeCore → compute路径
- 更新测试include路径

### Step 4: 更新代码引用
- 更新所有`#include "ComputeCore/..."`为`#include "compute/..."`
- 更新所有`#include "KeyhuntCore/..."`为新路径

### Step 5: 删除KeyhuntCore
```bash
rm -rf src/KeyhuntCore
```

---

## 🎯 预期收益

### 代码简化
- **减少代码量**: 删除~2000行重复代码
- **清晰的模块边界**: 每个模块职责明确
- **降低维护成本**: 减少15%的代码复杂度

### 性能提升
- **编译时间**: 减少10-15%
- **二进制大小**: 减少5-10%

### 可维护性
- **清晰的依赖关系**: 单向依赖，无循环
- **易于理解**: 新开发者快速上手
- **易于扩展**: 模块化设计便于添加新功能

---

## ⚠️ 风险评估

### 高风险
- **测试失败**: 迁移后测试可能失败
- **性能回归**: 路径变化可能影响性能

### 缓解措施
- **渐进式迁移**: 分阶段执行，每阶段验证
- **完整测试**: 每步都运行完整测试套件
- **性能基准**: 每步都运行性能基准测试
- **Git分支**: 在独立分支上执行，便于回滚

---

## 📋 检查清单

### 阶段1: 分析依赖关系
- [ ] 分析KeyhuntCore文件使用情况
- [ ] 分析ComputeCore依赖关系
- [ ] 识别重复代码

### 阶段2: 迁移有用代码
- [ ] 迁移benchmarks/
- [ ] 迁移utils/
- [ ] 废弃重复代码

### 阶段3: 重命名ComputeCore
- [ ] 重命名目录
- [ ] 更新引用路径
- [ ] 更新CMakeLists.txt

### 阶段4: 清理KeyhuntCore
- [ ] 删除未使用文件
- [ ] 更新测试引用
- [ ] 更新文档

### 阶段5: 验证和测试
- [ ] 编译成功
- [ ] 单元测试通过
- [ ] 集成测试通过
- [ ] 性能基准测试通过

---

## 📅 时间计划

| 阶段 | 工作量 | 开始时间 | 预计完成 |
|------|--------|---------|---------|
| 阶段1: 分析依赖 | 2h | 2025-10-13 09:00 | 2025-10-13 11:00 |
| 阶段2: 迁移代码 | 16h | 2025-10-13 11:00 | 2025-10-14 19:00 |
| 阶段3: 重命名 | 8h | 2025-10-14 19:00 | 2025-10-15 11:00 |
| 阶段4: 清理 | 4h | 2025-10-15 11:00 | 2025-10-15 15:00 |
| 阶段5: 验证 | 10h | 2025-10-15 15:00 | 2025-10-16 09:00 |

**总计**: 40小时（5个工作日）

---

**文档创建时间**: 2025-10-13 09:00
**文档作者**: AI Agent (Augment Code)
**任务状态**: 计划阶段


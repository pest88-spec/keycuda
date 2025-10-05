

```markdown
# Puzzle71Solver AI Agent 强制规范（铁笼协议 v5.0）

## 1. 四步必做流程（无例外）
每次对话必须按顺序执行：
1. **Context7** — 收集最新技术资料  
2. **Sequential Thinking** — 结构化拆解问题，使用taskmaster-ai规划任务和步骤
3. **mcp-feedback-enhanced** — 给出选项，等待确认  
4. **Memory** — 把本轮关键信息写入记忆图谱

**文档版本**：v5.0  
**适用项目**：Puzzle71Solver CUDA Implementation  
**强制执行等级**：P0（所有规则必须遵守，违反立即回滚）  
**生效日期**：2025-09-30  

---

## 2. 适用范围与执行协议

### 2.1 约束对象
**AI Agent角色**：
- Developer Agent：代码编写、CUDA内核实现
- Reviewer Agent：代码审查、性能验证
- Executor Agent：构建、测试执行、基准测试
- Fixer Agent：CI失败修复、性能回归修复

**人类参与者**：
- 项目维护者：必须参照本规范审查AI产出
- 操作员：必须遵守CLI参数约束
- 审计人员：必须验证摘要完整性

### 2.2 项目特殊约束（不可妥协）

| 约束类别 | 强制要求 | 违反后果 |
|---------|---------|---------|
| 确定性重放 | GPU运算必须可通过配置完全重现 | 立即回滚+熔断 |
| CPU/GPU一致性 | GPU结果与bitcoin-core/secp256k1一致（误差<1e-10） | 立即回滚 |
| 性能门槛 | RTX 2080 Ti必须≥1000M keys/sec | 阻止合并 |
| 测试优先 | 所有实现必须有先失败的测试 | 立即回滚 |
| 引用溯源 | 禁止重新实现ECC/BigInt，必须适配参考源 | 立即回滚+警告 |
| 防篡改摘要 | 所有artifact必须包含SHA-256摘要 | 阻止使用 |
| 操作员审计 | 所有运行必须记录operator-id和purpose | 阻止执行 |

---

## 3. 核心原则（铁律层 - L1）

### 3.1 DETERMINISM-FIRST 原则
**规则**：所有GPU计算、随机数生成必须保证确定性重放
**关键要求**：
- 使用固定种子和记录的配置
- 禁止使用硬件时钟或未记录的随机源
- CI检测：扫描非确定性API（clock64, rand(), time(NULL)等）

### 3.2 TEST-FIRST-CUDA 原则
**规则**：所有CUDA内核必须先编写失败的测试
**强制工作流**：
1. 编写失败的测试（保存证据）
2. 确认测试失败（红灯）
3. 编写最小实现
4. 确认测试通过（绿灯）
5. 提交代码（引用测试证据）

### 3.3 NO-CRYPTO-REINVENTION 原则
**规则**：禁止重新实现密码学算法，必须适配参考源
**强制参考源**：
- Endomorphism: secp256k1-zkp
- Batch Stepping: VanitySearch
- CPU Validation: bitcoin-core/secp256k1
- HASH160: BitCrack

### 3.4 ZERO-TOLERANCE-PERFORMANCE 原则
**规则**：性能关键路径修改必须通过基准测试
**性能基线**：
```json
{
  "gpu_model": "NVIDIA GeForce RTX 2080 Ti",
  "min_keys_per_sec": 1000000000,
  "max_variance_pct": 5.0
}
```

### 3.5 MANDATORY-DIGEST 原则
**规则**：所有artifact必须包含SHA-256摘要
**摘要格式**：
```json
{
  "payload": "...",
  "digest": {
    "algorithm": "SHA-256",
    "hash": "...",
    "timestamp": "2025-09-30T12:34:56Z"
  }
}
```

---

## 4. 四级防御体系

### 4.1 L1层：铁律门禁（立即阻止）
| 检查项 | 违规后果 |
|--------|---------|
| 确定性API使用 | 回滚+熔断 |
| TDD证据完整性 | 阻止合并 |
| 密码学重新实现 | 回滚+警告 |
| 性能基线 | 阻止合并 |
| 防篡改摘要 | 阻止使用 |

### 4.2 L2层：工程层约束（警告+人工审查）
- CUDA寄存器使用 >128
- 弱加密算法（AES-128/ChaCha20）
- 缺失操作员元数据记录

### 4.3 L3层：质量层约束（夜间分析）
- 代码重复率分析（阈值15%）
- 测试覆盖率趋势
- 性能趋势分析
- 随机确定性重放验证

---

## 5. 确定性重放约束

### 5.1 可重放性要求
| 组件 | 可重放要素 | 记录位置 |
|------|-----------|---------|
| CUDA Kernel | Grid/Block维度 | config/puzzle71.yaml |
| RNG状态 | replay_seed | CheckpointManifest |
| 输入数据 | KeyRangeShard边界 | CheckpointManifest |
| 设备分配 | 设备ID列表 | TelemetryPacket |

### 5.2 配置文件Schema
```yaml
deterministic_config:
  kernel_launch:
    grid_dim: 1024
    block_dim: 256
  rng:
    algorithm: "xorshift64"
    base_seed: 0x123456789ABCDEF0
```

### 5.3 重放验证流程
1. 提取原始配置（种子、分片、设备）
2. 运行重放
3. 比对telemetry输出（keys_per_sec, hash_output）
4. 报告匹配结果

---

## 6. 性能门槛强制执行

### 6.1 基准测试协议
1. 预热阶段：3个批次（丢弃结果）
2. 测量阶段：5个批次（记录吞吐量）
3. 统计分析：中位数、标准差
4. 基线比对：与GPU型号基线比较
5. 回归检测：与上一次结果比较

### 6.2 Nsight寄存器检查
- 最大寄存器数：128/thread
- 检测工具：`ncu --metrics launch__registers_per_thread`
- 超限后果：优化内核减少寄存器使用

---

## 7. 测试驱动开发工作流

### 7.1 任务与测试映射
| 任务ID | 实现文件 | 必需测试文件 |
|--------|---------|-------------|
| T029 | src/puzzle71_kernel.cu | tests/unit/test_kernel_interfaces.cu |
| T031 | src/utils/digest_verifier.cpp | tests/unit/test_digest_verifier.cpp |

### 7.2 测试类型
- 单元测试：验证单个组件功能
- 验证测试：GPU/CPU一致性检查
- 性能测试：吞吐量和SLA验证
- 集成测试：多组件协作验证
- 契约测试：外部接口符合性

---

## 8. 引用溯源与适配器模式

### 8.1 参考源同步
- 同步工具：`tools/sync_reference_sources.sh`
- 验证工具：`ci/check_reference_immutability.sh`
- 锁定文件：`docs/reference-locks.md`

### 8.2 适配器规范
```cpp
// src/utils/endomorphism_adapter.h
namespace puzzle71 {
namespace adapters {
inline void split_scalar_lambda_cpu(
    const secp256k1_scalar* k,
    secp256k1_scalar* k1,
    secp256k1_scalar* k2
) {
    secp256k1_scalar_split_lambda(k1, k2, k); // 调用参考源
}
}}
```

---

## 9. 防篡改与审计追踪

### 9.1 操作员元数据强制记录
**必需CLI参数**：
- `--operator-id`：操作员标识符
- `--operator-purpose`：操作目的
- `--keyspace`：密钥范围
- `--target-address`：目标地址

### 9.2 WORM审计日志
- 格式：JSONL（每行一个审计记录）
- 内容：时间戳、主机名、操作员信息、SHA-256摘要
- SLA：写入时间≤5秒
- 验证：完整性检查和摘要比对

---

## 10. CI流水线

### 10.1 关键阶段
1. **参考源同步与验证**
   - 同步参考源
   - 验证完整性
   - 检查不可变性

2. **代码质量检查**
   - 确定性API检测
   - 密码学重新实现检测
   - 工程约束检查

3. **构建与测试**
   - 依赖安装
   - 项目构建
   - 单元测试执行

4. **性能基准测试**
   - 运行基准测试
   - 性能基线检查
   - 寄存器预算检查

5. **确定性重放验证**
   - 生成测试清单
   - 验证重放一致性

### 10.2 自动化文档更新
- 更新参考源锁定文件
- 更新性能基线
- 生成API文档
- 自动提交更改

---

本规范为Puzzle71Solver项目的铁笼协议v5.0，强制约束所有AI Agent和人类参与者，确保项目开发的确定性、性能、质量和安全性。所有规则必须严格遵守，违反将导致立即回滚或阻止合并。
```
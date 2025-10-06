# 方案A执行回滚指南 (Plan A Rollback Guide)

⚠️ **紧急恢复文档** ⚠️

本文档记录了在方案A（代码提取重构）执行失败时的完整恢复步骤。

---

## 备份信息概览

### 备份点信息

| 项目 | 值 | 说明 |
|------|---|------|
| **备份Tag** | `v0.2.0-pre-extraction-backup` | 完整快照标签 |
| **备份分支** | `backup-before-extraction` | 独立备份分支 |
| **Commit Hash** | `88951fb` | 最后安全状态 |
| **创建时间** | 2025-10-06 | 方案A执行前 |
| **远程仓库** | `github.com:pest88-spec/keycuda.git` | 已推送 ✅ |

### 备份内容清单

```
✅ third_party/BitCrack/           # 完整submodule
✅ third_party/CudaBrainSecp/      # 完整submodule
✅ src/puzzle71_kernel.cu          # 148 regs kernel
✅ src/solver.cpp                  # AsyncCheckpointWriter
✅ performance_analysis.md         # Phase A分析
✅ puzzle71_constraints.md         # 更新后的规范
✅ 所有源代码和配置文件
```

---

## 回滚场景与对策

### 场景1: 代码提取错误（推荐方法）

**症状**: 提取的代码编译失败、功能不正确

**解决方案**: 使用Tag恢复（保留当前分支的后续commit）

```bash
# 1. 查看当前损坏状态
git status
git log --oneline -5

# 2. 检出备份tag到新分支
git checkout v0.2.0-pre-extraction-backup
git checkout -b recovery-from-extraction

# 3. 验证恢复成功
ls third_party/  # 应显示: BitCrack  CudaBrainSecp
git log -1 --oneline  # 应显示: 88951fb

# 4. 如需覆盖原分支
git branch -D 001-implement-puzzle71solver-mred
git checkout -b 001-implement-puzzle71solver-mred
git push origin 001-implement-puzzle71solver-mred --force
```

---

### 场景2: 本地分支完全损坏（强力恢复）

**症状**: git状态混乱、冲突无法解决

**解决方案**: 硬重置到备份点

```bash
# ⚠️ 警告：此操作会丢失v0.2.0-pre-extraction-backup之后的所有commit

# 1. 确认要重置的分支
git branch  # 确保在 001-implement-puzzle71solver-mred

# 2. 硬重置到备份tag
git reset --hard v0.2.0-pre-extraction-backup

# 3. 验证
git log -1 --oneline  # 应显示: 88951fb
ls third_party/        # 应显示: BitCrack  CudaBrainSecp

# 4. 强制推送到远程（覆盖远程损坏状态）
git push origin 001-implement-puzzle71solver-mred --force
```

---

### 场景3: 从远程完全重新克隆（最彻底）

**症状**: 本地仓库完全不可用

**解决方案**: 删除本地，从备份分支克隆

```bash
# 1. 备份当前工作（如果有未提交的重要修改）
cd D:/mybitcoin/puzzlekeyhunt/PuzzleKeyhunt
cp -r . ../PuzzleKeyhunt-local-backup  # Windows: xcopy /E /I . ..\PuzzleKeyhunt-local-backup

# 2. 删除损坏的本地仓库
cd ..
rm -rf PuzzleKeyhunt  # Windows: rmdir /S /Q PuzzleKeyhunt

# 3. 从备份分支克隆
git clone -b backup-before-extraction git@github.com:pest88-spec/keycuda.git PuzzleKeyhunt

# 4. 切换到工作分支
cd PuzzleKeyhunt
git checkout 001-implement-puzzle71solver-mred
git reset --hard v0.2.0-pre-extraction-backup

# 5. 验证
ls third_party/  # 应显示: BitCrack  CudaBrainSecp
```

---

### 场景4: 仅恢复third_party/目录

**症状**: 只有third_party被错误删除，其他正常

**解决方案**: 部分文件恢复

```bash
# 1. 从备份tag恢复特定目录
git checkout v0.2.0-pre-extraction-backup -- third_party/

# 2. 验证恢复
ls third_party/  # 应显示: BitCrack  CudaBrainSecp
git status       # 显示: Changes to be committed

# 3. 提交恢复
git commit -m "Restore third_party/ from backup tag v0.2.0-pre-extraction-backup"
git push origin 001-implement-puzzle71solver-mred
```

---

### 场景5: submodule损坏

**症状**: third_party/目录存在但submodule内容损坏

**解决方案**: 重新初始化submodules

```bash
# 1. 清理损坏的submodule
git submodule deinit -f --all
rm -rf .git/modules/third_party

# 2. 从备份恢复.gitmodules
git checkout v0.2.0-pre-extraction-backup -- .gitmodules

# 3. 重新初始化submodules
git submodule update --init --recursive

# 4. 验证
cd third_party/BitCrack && git status  # 应显示clean状态
cd ../CudaBrainSecp && git status
```

---

## 验证恢复成功的检查清单

恢复后，按顺序执行以下检查：

### ✅ 1. Git状态检查

```bash
# 检查commit
git log -1 --oneline
# 预期输出: 88951fb Update project constraints: migrate from snapshots to code extraction

# 检查分支
git branch --show-current
# 预期输出: 001-implement-puzzle71solver-mred (或 recovery-from-extraction)

# 检查远程同步
git fetch origin
git status
# 预期输出: Your branch is up to date with 'origin/...'
```

### ✅ 2. 文件结构检查

```bash
# 验证third_party存在
ls third_party/
# 预期输出:
# BitCrack
# CudaBrainSecp

# 验证子模块完整
cd third_party/BitCrack && ls | head -5
# 应显示BitCrack的源文件

cd ../CudaBrainSecp && ls | head -5
# 应显示CudaBrainSecp的源文件

cd ../..
```

### ✅ 3. 编译测试

```bash
# 清理旧构建
rm -rf build
mkdir build && cd build

# 重新构建
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 预期：编译成功，无错误
```

### ✅ 4. 性能基线验证

```bash
# 运行性能测试
./Puzzle71Solver \
    --keyspace 0x400000000000000000:0x40000000000FFFFFF \
    --target-address 19vkiEajfhuZ8bs8Zu2jgmC6oqZbWqhxhG \
    --operator-id recovery-test \
    --operator-purpose verification \
    --device 0 \
    --super

# 预期输出:
# - Throughput: ~840 Mkeys/s (稳定)
# - Grid: 624 blocks
# - Registers: 148 regs/thread
# - 功能正常
```

---

## 关键文件备份清单

如果需要手动恢复，以下文件必须从备份tag恢复：

### P0级文件（必须恢复）

```bash
# 核心源码
src/puzzle71_kernel.cu           # Fused kernel (148 regs)
src/solver.cpp                   # AsyncCheckpointWriter
src/KeyhuntCore/gpu/batch_planner.h  # kMaxPointsPerThread=1024

# 依赖
third_party/BitCrack/            # 完整目录
third_party/CudaBrainSecp/       # 完整目录
.gitmodules                      # submodule配置

# 构建配置
CMakeLists.txt                   # 构建脚本
```

### P1级文件（重要但可重建）

```bash
performance_analysis.md          # Phase A分析
puzzle71_constraints.md          # 项目规范
test-rule.md                     # AI开发规范
```

---

## 紧急联系信息

### GitHub仓库信息

```
Repository: https://github.com/pest88-spec/keycuda
Branch: 001-implement-puzzle71solver-mred
Backup Branch: backup-before-extraction
Backup Tag: v0.2.0-pre-extraction-backup
```

### 备份位置验证

```bash
# 验证远程备份存在
git ls-remote --tags origin | grep v0.2.0-pre-extraction-backup
# 预期输出:
# 354342457035df6cc1b66ddc5224bda420e033c2	refs/tags/v0.2.0-pre-extraction-backup
# 88951fb25f775bcc0e7b85b9bc5743b46a4c69c9	refs/tags/v0.2.0-pre-extraction-backup^{}

git ls-remote --heads origin | grep backup-before-extraction
# 预期输出:
# 88951fb25f775bcc0e7b85b9bc5743b46a4c69c9	refs/heads/backup-before-extraction
```

---

## 预防措施（执行方案A时必读）

### ❌ 绝对禁止的操作

1. **不要直接在主分支操作**
   ```bash
   # ❌ 错误
   git checkout main
   rm -rf third_party/

   # ✅ 正确
   git checkout -b plan-a-extraction
   # 在新分支操作
   ```

2. **不要跳过编译验证**
   ```bash
   # ❌ 错误
   git add .
   git commit -m "Extract code"
   git push

   # ✅ 正确
   # 1. 提取代码
   # 2. 编译测试
   # 3. 性能测试
   # 4. 确认成功后才commit
   ```

3. **不要删除备份**
   ```bash
   # ❌ 绝对禁止
   git branch -D backup-before-extraction
   git tag -d v0.2.0-pre-extraction-backup
   git push origin :backup-before-extraction
   git push origin :v0.2.0-pre-extraction-backup
   ```

### ✅ 建议的安全操作流程

```bash
# 1. 创建实验分支
git checkout -b plan-a-extraction-test

# 2. 执行代码提取
# ... 提取操作 ...

# 3. 编译验证
mkdir build && cd build && cmake .. && make -j$(nproc)

# 4. 功能测试
./Puzzle71Solver --keyspace 0x1:0x1000 --target-address ... --device 0

# 5. 性能测试
./scripts/run-benchmarks.sh 0 5

# 6. 确认成功后合并
git checkout 001-implement-puzzle71solver-mred
git merge plan-a-extraction-test

# 7. 推送
git push origin 001-implement-puzzle71solver-mred

# 8. 如果失败，立即回滚
git reset --hard v0.2.0-pre-extraction-backup
```

---

## 最坏情况恢复（终极方案）

如果所有方法都失败，执行以下步骤：

```bash
# 1. 从GitHub直接下载备份tag的zip
# 访问: https://github.com/pest88-spec/keycuda/archive/refs/tags/v0.2.0-pre-extraction-backup.zip

# 2. 解压到新目录
unzip v0.2.0-pre-extraction-backup.zip
cd keycuda-0.2.0-pre-extraction-backup

# 3. 初始化git（如果需要）
git init
git remote add origin git@github.com:pest88-spec/keycuda.git
git fetch --all
git checkout -b 001-implement-puzzle71solver-mred
git reset --hard v0.2.0-pre-extraction-backup

# 4. 重新初始化submodules
git submodule update --init --recursive

# 5. 推送覆盖远程
git push origin 001-implement-puzzle71solver-mred --force
```

---

## 回滚决策树

```
方案A执行失败？
├─ 是 → 编译失败？
│        ├─ 是 → 功能错误？
│        │        ├─ 是 → 使用场景2（硬重置）
│        │        └─ 否 → 使用场景4（恢复third_party/）
│        └─ 否 → Git状态混乱？
│                 ├─ 是 → 使用场景3（重新克隆）
│                 └─ 否 → 使用场景1（Tag恢复）
└─ 否 → 继续方案A
```

---

## 总结

**3层安全保障**:
1. ✅ **Tag备份**: `v0.2.0-pre-extraction-backup`
2. ✅ **分支备份**: `backup-before-extraction`
3. ✅ **远程备份**: GitHub已推送

**零数据丢失承诺**:
即使方案A完全失败，通过本文档可100%恢复到88951fb状态。

**恢复时间**:
- 场景1/4: < 5分钟
- 场景2: < 2分钟
- 场景3: < 10分钟
- 场景5: < 15分钟

---

**文档创建时间**: 2025-10-06
**最后更新**: 2025-10-06
**维护者**: Puzzle71Solver Team
**状态**: ✅ 已验证所有恢复路径

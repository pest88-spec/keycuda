# 并行开发策略 - 当前项目与融合项目共存

**文档版本**: v1.0  
**创建日期**: 2025-10-12  
**目标**: 确保当前开发工作与融合项目互不干扰  

---

## 🎯 核心策略：Git分支隔离

### 分支结构设计

```
main (主分支 - 稳定版本)
  ├── develop (当前开发分支 - 您正在编码)
  │   ├── feature/your-current-work-1
  │   ├── feature/your-current-work-2
  │   └── bugfix/some-fix
  │
  └── feature/super-solver-fusion (融合项目分支 - 独立演进)
      ├── fusion/stage1-infrastructure
      ├── fusion/stage2-core-layer
      ├── fusion/stage3-algorithm-layer
      └── ...
```

**关键原则**:
- ✅ 当前开发工作在`develop`分支及其子分支
- ✅ 融合项目在`feature/super-solver-fusion`分支及其子分支
- ✅ 两个分支完全隔离，互不影响
- ✅ 定期从`main`合并到两个分支，保持同步

---

## 📋 安全并行开发工作流

### 步骤1: 保护当前工作

**在开始融合项目前，先保护当前工作**:

```bash
cd d:/mybitcoin/puzzlekeyhunt/PuzzleKeyhunt

# 1. 确认当前分支
git branch
# 输出应该显示您当前所在的分支（例如：develop或feature/xxx）

# 2. 提交当前所有未提交的更改
git status
git add .
git commit -m "WIP: Save current work before starting fusion project"

# 3. 推送到远程（如果有）
git push origin <your-current-branch>

# 4. 创建当前工作的备份分支
git checkout -b backup/current-work-$(date +%Y%m%d-%H%M%S)
git push origin backup/current-work-$(date +%Y%m%d-%H%M%S)

# 5. 回到原来的工作分支
git checkout <your-current-branch>
```

---

### 步骤2: 创建融合项目分支

**从main分支创建融合项目分支**:

```bash
# 1. 切换到main分支
git checkout main

# 2. 拉取最新代码
git pull origin main

# 3. 创建融合项目分支
git checkout -b feature/super-solver-fusion

# 4. 推送到远程
git push -u origin feature/super-solver-fusion
```

---

### 步骤3: 在融合分支上工作

**所有融合项目的工作都在这个分支上进行**:

```bash
# 确认在融合分支
git branch
# 应该显示：* feature/super-solver-fusion

# 创建目录结构（不会影响其他分支）
mkdir -p external/{VanitySearch,BitCrack,Keyhunt,secp256k1,nccl}
mkdir -p src/Core/{ECC,Hash,Memory}
# ... 其他目录

# 提交更改
git add .
git commit -m "feat(fusion): Create directory structure for fusion project"
git push origin feature/super-solver-fusion
```

---

### 步骤4: 切换回当前工作

**随时可以切换回当前工作分支**:

```bash
# 1. 提交融合分支的更改
git add .
git commit -m "feat(fusion): Work in progress"
git push origin feature/super-solver-fusion

# 2. 切换回当前工作分支
git checkout <your-current-branch>

# 3. 继续您的当前工作
# 此时看到的代码完全是您之前的状态，融合项目的更改不可见
```

---

### 步骤5: 在两个分支间切换

**可以随时在两个分支间切换**:

```bash
# 切换到融合分支
git checkout feature/super-solver-fusion

# 切换回当前工作分支
git checkout <your-current-branch>

# 查看所有分支
git branch -a
```

---

## 🛡️ 安全保障措施

### 措施1: 使用.gitignore隔离构建产物

**在融合分支添加.gitignore**:

```bash
# 在feature/super-solver-fusion分支
cat >> .gitignore << 'EOF'

# 融合项目特定忽略
external/*/build/
external/*/.git/
src/Core/
src/Algorithm/
src/GPU/
src/Storage/
src/Interface/
audit/
benchmarks/telemetry/
EOF

git add .gitignore
git commit -m "feat(fusion): Add .gitignore for fusion project"
```

---

### 措施2: 使用Git Worktree（高级选项）

**如果需要同时查看两个分支的代码**:

```bash
# 在主项目目录
cd d:/mybitcoin/puzzlekeyhunt/PuzzleKeyhunt

# 创建worktree（在不同目录）
git worktree add ../PuzzleKeyhunt-Fusion feature/super-solver-fusion

# 现在有两个独立的工作目录：
# d:/mybitcoin/puzzlekeyhunt/PuzzleKeyhunt (当前工作)
# d:/mybitcoin/puzzlekeyhunt/PuzzleKeyhunt-Fusion (融合项目)

# 在融合项目目录工作
cd ../PuzzleKeyhunt-Fusion
# 这里的所有更改只影响feature/super-solver-fusion分支

# 在当前项目目录工作
cd ../PuzzleKeyhunt
# 这里的所有更改只影响您当前的分支
```

**优势**:
- ✅ 两个目录完全独立
- ✅ 可以同时打开两个IDE窗口
- ✅ 互不干扰
- ✅ 可以同时编译两个版本

---

### 措施3: 定期同步main分支

**保持两个分支都与main同步**:

```bash
# 每周或每两周执行一次

# 1. 更新main分支
git checkout main
git pull origin main

# 2. 合并到当前工作分支
git checkout <your-current-branch>
git merge main
git push origin <your-current-branch>

# 3. 合并到融合分支
git checkout feature/super-solver-fusion
git merge main
git push origin feature/super-solver-fusion
```

---

## 📊 并行开发时间线示例

### 第1周（当前）

| 时间 | 当前工作分支 | 融合项目分支 |
|------|------------|------------|
| 周一 | 继续编码功能A | - |
| 周二 | 继续编码功能A | - |
| 周三 | 测试功能A | 创建分支、目录结构 |
| 周四 | 修复bug | 克隆第三方库 |
| 周五 | 提交功能A | CMake集成 |

### 第2周

| 时间 | 当前工作分支 | 融合项目分支 |
|------|------------|------------|
| 周一 | 开始功能B | CI/CD配置 |
| 周二 | 继续功能B | 提取VanitySearch GLV |
| 周三 | 继续功能B | 编写GLV单元测试 |
| 周四 | 测试功能B | 提取BitCrack哈希 |
| 周五 | 提交功能B | 编写哈希单元测试 |

**关键点**:
- ✅ 两个分支独立推进
- ✅ 可以根据时间灵活分配
- ✅ 互不干扰

---

## 🚨 常见问题与解决方案

### Q1: 如果不小心在错误的分支上提交了代码怎么办？

**解决方案**:

```bash
# 假设您在融合分支上不小心提交了当前工作的代码

# 1. 撤销最后一次提交（保留更改）
git reset --soft HEAD~1

# 2. 暂存更改
git stash

# 3. 切换到正确的分支
git checkout <your-current-branch>

# 4. 应用暂存的更改
git stash pop

# 5. 重新提交
git add .
git commit -m "Your commit message"
```

---

### Q2: 如何查看两个分支的差异？

**解决方案**:

```bash
# 查看两个分支的文件差异
git diff <your-current-branch> feature/super-solver-fusion

# 查看两个分支的提交差异
git log <your-current-branch>..feature/super-solver-fusion

# 查看两个分支的文件列表差异
git diff --name-only <your-current-branch> feature/super-solver-fusion
```

---

### Q3: 如何合并融合分支的某些更改到当前分支？

**解决方案**:

```bash
# 使用cherry-pick选择性合并提交

# 1. 在融合分支查看提交历史
git checkout feature/super-solver-fusion
git log --oneline

# 2. 记下想要合并的提交hash（例如：abc123）

# 3. 切换到当前分支
git checkout <your-current-branch>

# 4. cherry-pick该提交
git cherry-pick abc123

# 5. 推送
git push origin <your-current-branch>
```

---

## ✅ 推荐工作流程

### 日常开发

**上午（当前工作）**:
```bash
git checkout <your-current-branch>
# 编码、测试、提交
git add .
git commit -m "feat: Implement feature X"
git push origin <your-current-branch>
```

**下午（融合项目）**:
```bash
git checkout feature/super-solver-fusion
# 提取第三方库、编写适配器、测试
git add .
git commit -m "feat(fusion): Extract VanitySearch GLV"
git push origin feature/super-solver-fusion
```

---

## 📋 检查清单

**开始融合项目前**:
- [ ] 提交当前所有未提交的更改
- [ ] 创建当前工作的备份分支
- [ ] 从main创建feature/super-solver-fusion分支
- [ ] 推送融合分支到远程

**日常开发**:
- [ ] 明确当前在哪个分支（git branch）
- [ ] 提交前确认分支正确
- [ ] 定期推送到远程
- [ ] 每周同步main分支

**切换分支前**:
- [ ] 提交或暂存当前更改
- [ ] 确认没有未提交的文件
- [ ] 切换分支后检查代码状态

---

## 🎯 总结

**核心原则**: Git分支隔离，互不干扰

**推荐方案**: 使用Git Worktree，在不同目录工作

**安全保障**: 定期备份、定期同步、明确分支

**您可以放心地**:
- ✅ 继续在当前分支编码
- ✅ 在融合分支进行融合项目
- ✅ 随时切换，互不影响
- ✅ 两个项目独立推进

---

**文档作者**: AI Agent (Augment Code)  
**文档版本**: v1.0  
**创建日期**: 2025-10-12


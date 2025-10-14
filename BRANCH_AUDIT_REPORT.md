# 分支审计与清理建议（加强版）

本报告基于仓库当前 HEAD（提交 `fab34237fd4656cf078a8c190f1095d2219adf3e`，分支 `work`），完整检查现有 Git 引用并对照文档要求给出修复方案。

## 1. Git 引用总览

### 1.1 本地分支（`git branch -avv`）

| 分支 | 类型 | HEAD 提交 | 提交时间 (UTC) | 最近提交摘要 | 额外说明 |
|------|------|-----------|----------------|--------------|----------|
| `work` | 本地 | `fab3423` | 2025-10-14 | Translate branch audit report to Chinese | 仓库唯一分支，累计 99 次提交（`git rev-list --count work`）。 |

### 1.2 远程分支与上游配置

```
$ git remote -v
# （无输出，未配置任何远程）
```

仓库缺少远程仓库配置，因此无法从远端检出或同步任何分支。

### 1.3 标签

```
$ git tag
# （无输出，未定义任何标签）
```

### 1.4 Git 引用完整性

```
$ git show-ref
fab34237fd4656cf078a8c190f1095d2219adf3e refs/heads/work
```

`refs/heads` 下仅存在 `work`，没有额外的历史分支快照。

## 2. 文档要求与现状差距

| 文档 | 期望的分支/标签 | 现状 | 影响 |
|------|-----------------|------|------|
| `README.md` | 指示使用远程分支 `001-implement-puzzle71solver-mred` | 缺少该分支和远程配置 | 新开发者按文档操作会失败。 |
| `puzzle71_constraints.md` | 强制 `main`、`develop` 与 `001-implement-puzzle71solver-mred` 并行存在 | 实际仅有 `work` | 约束无法落地，CI 配置失效。 |
| `ROLLBACK-PLAN-A.md` | 依赖分支 `backup-before-extraction` 与标签 `v0.2.0-pre-extraction-backup` | 均不存在 | 回滚流程不可用。 |
| 脚本与 CI (`ci/`, `scripts/`) | 引用 `main`/`develop` 等稳定分支 | 缺少对应分支 | 自动化流程与文档严重偏离。 |

## 3. 历史痕迹与缺失分支线索

1. **提交记录**：提交 `b328d15` 的说明包含 "Merge branch '001-implement-puzzle71solver-mred'"，表明历史上确有该分支，但目前未保留引用。
2. **回滚脚本**：`ROLLBACK-PLAN-A.md` 多处命令假设 `recovery-from-extraction` 或 `001-implement-puzzle71solver-mred` 已存在，可作为恢复分支的名称候选。
3. **CI 配置**：`puzzle71_constraints.md` 在多个 YAML 片段中声明 `branches: [main, develop, 001-implement-puzzle71solver-mred]`，进一步证明应维护至少三个长期分支。

## 4. 发布时间线（Plan A 抽取）

| 阶段 | 提交范围 | 关键事件 |
|------|----------|----------|
| Plan A 前的稳定阶段 | `0565ff4` – `696967e` | 完成 GPU 对齐、性能与稳定性修复，为抽取前做准备。 |
| Plan A 准备阶段 | `714b1a2` – `88951fb` | 建立回滚计划、调整架构约束，准备 BitCrack 抽取。 |
| Plan A 执行与清理 | `8f9a486` – `97f1c27` | 完成抽取、更新构建/CI、清理遗留产物并中和术语。 |
| 近期文档更新 | `97f1c27` – `fab3423` | 仅包含分支审计报告的英文与中文版本，没有新的代码变更。 |

缺乏标签使这些阶段无法快速定位，严重影响回滚和版本对齐能力。

## 5. 建议的修复步骤

1. **恢复发布主干**
   - 在当前提交 `fab3423` 上创建 `main`（`git branch main fab3423`），将其作为发布主干。
2. **重建开发分支**
   - 将现有 `work` 重命名为 `develop`（`git branch -m work develop`），或在 `main` 基础上新建 `develop` 并迁移日常开发。
3. **补建回滚安全网**
   - 针对提交 `88951fb`：
     - 创建标签 `v0.2.0-pre-extraction-backup`。
     - 创建分支 `backup-before-extraction`（或文档中的 `recovery-from-extraction`），与回滚手册保持一致。
4. **修复遗留特性分支**
   - 查找提交 `b328d15` 附近的历史，定位 `001-implement-puzzle71solver-mred` 的预期指向（通常为该提交父节点），重新创建该分支用于保存 Puzzle #71 特性工作成果。
5. **补齐旧版发布标签**
   - 为抽取前最后稳定提交 `0565ff4` 创建标签，例如 `v0.1.0`，以便保留旧架构快照。
6. **更新文档与 CI**
   - 待上述引用创建后，修订 `README.md`、`puzzle71_constraints.md`、`ROLLBACK-PLAN-A.md` 以及相关脚本，确保说明与真实分支/标签一致。
7. **建立远程与访问策略**
   - 配置远程仓库（`git remote add origin <url>`），并按照文档需求推送 `main`/`develop`/特性分支与标签，恢复团队协作能力。
8. **定期审计与自动化**
   - 建议新增脚本或 CI 作业，周期性运行 `git for-each-ref` 与一致性检查，防止分支再次漂移。

执行以上步骤后，仓库的 Git 结构将与文档描述重新对齐，回滚方案可以重新启用，新成员也能依照文档顺利展开工作。

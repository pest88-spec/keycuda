#!/bin/bash
# 文档完整性验证脚本
# 用途：验证项目规范文档是否完整存在
# 作者：PuzzleKeyhunt Team
# 日期：2025-10-12

set -e  # 遇到错误立即退出

echo "========================================"
echo "📚 项目文档完整性验证"
echo "========================================"
echo ""

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 统计变量
total_files=0
found_files=0
missing_files=0

# 验证单个文件
verify_file() {
    local file_path=$1
    local file_desc=$2
    total_files=$((total_files + 1))

    if [ -f "$file_path" ]; then
        size=$(wc -c < "$file_path")
        lines=$(wc -l < "$file_path")
        echo -e "${GREEN}✅${NC} $file_desc"
        echo "   路径: $file_path"
        echo "   大小: $size bytes, $lines lines"
        found_files=$((found_files + 1))
    else
        echo -e "${RED}❌${NC} $file_desc"
        echo "   路径: $file_path (不存在)"
        missing_files=$((missing_files + 1))
    fi
    echo ""
}

# 1. 验证核心规范文档
echo "=== 核心规范文档（Speckit工具链）==="
echo ""

verify_file "specs/003-gpu-1-28/spec.md" "特性规范 (spec.md)"
verify_file "specs/003-gpu-1-28/plan.md" "实施计划 (plan.md)"
verify_file "specs/003-gpu-1-28/tasks.md" "任务列表 (tasks.md)"
verify_file "specs/003-gpu-1-28/data-model.md" "数据模型 (data-model.md)"
verify_file ".specify/memory/constitution.md" "项目宪法 (constitution.md)"

# 2. 验证任务完成度
echo "=== 任务完成度验证 ==="
echo ""

if [ -f "specs/003-gpu-1-28/tasks.md" ]; then
    total_tasks=$(grep -c "^- \[.\] T0" specs/003-gpu-1-28/tasks.md || echo "0")
    completed_tasks=$(grep -c "^- \[X\] T0" specs/003-gpu-1-28/tasks.md || echo "0")
    echo -e "${GREEN}✅${NC} 任务系统验证"
    echo "   总任务数: $total_tasks"
    echo "   已完成: $completed_tasks"
    if [ "$completed_tasks" -eq "$total_tasks" ]; then
        echo -e "   状态: ${GREEN}100% 完成${NC}"
    else
        incomplete=$((total_tasks - completed_tasks))
        echo -e "   状态: ${YELLOW}${incomplete} 个任务未完成${NC}"
    fi
else
    echo -e "${RED}❌${NC} 无法验证任务完成度 (tasks.md不存在)"
fi
echo ""

# 3. 验证需求覆盖
echo "=== 需求覆盖验证 ==="
echo ""

if [ -f "specs/003-gpu-1-28/spec.md" ]; then
    fr_count=$(grep -c "^- \*\*FR-0[0-9][0-9]\*\*" specs/003-gpu-1-28/spec.md || echo "0")
    nfr_count=$(grep -c "^- \*\*NFR-0[0-9][0-9]\*\*" specs/003-gpu-1-28/spec.md || echo "0")
    total_requirements=$((fr_count + nfr_count))
    echo -e "${GREEN}✅${NC} 需求系统验证"
    echo "   功能需求 (FR): $fr_count"
    echo "   非功能需求 (NFR): $nfr_count"
    echo "   总需求数: $total_requirements"
else
    echo -e "${RED}❌${NC} 无法验证需求覆盖 (spec.md不存在)"
fi
echo ""

# 4. 验证用户故事
echo "=== 用户故事验证 ==="
echo ""

if [ -f "specs/003-gpu-1-28/spec.md" ]; then
    us_count=$(grep -c "^### User Story [123]" specs/003-gpu-1-28/spec.md || echo "0")
    echo -e "${GREEN}✅${NC} 用户故事系统验证"
    echo "   用户故事数: $us_count"
    if [ "$us_count" -eq 3 ]; then
        echo -e "   状态: ${GREEN}完整 (US1, US2, US3)${NC}"
    else
        echo -e "   状态: ${YELLOW}不完整 (应该有3个用户故事)${NC}"
    fi
else
    echo -e "${RED}❌${NC} 无法验证用户故事 (spec.md不存在)"
fi
echo ""

# 5. 验证其他重要文档
echo "=== 其他重要文档 ==="
echo ""

verify_file "docs/GPU_OPTIMIZATION_GUIDE.md" "GPU优化指南"
verify_file "docs/reference-sources.md" "代码溯源文档"
verify_file "README.md" "项目README"
verify_file "CLAUDE.md" "AI开发指南"

# 6. 检查审计报告目录
echo "=== 审计报告目录 ==="
echo ""

if [ -d "docs/reviews" ]; then
    review_count=$(find docs/reviews -name "*.md" | wc -l)
    echo -e "${GREEN}✅${NC} 审计报告目录存在"
    echo "   报告数量: $review_count"
    if [ "$review_count" -gt 0 ]; then
        echo "   最近报告:"
        find docs/reviews -name "*.md" -type f | head -3 | sed 's/^/     - /'
    fi
else
    echo -e "${YELLOW}⚠️${NC}  审计报告目录不存在 (docs/reviews/)"
fi
echo ""

# 7. 检查性能基准目录
echo "=== 性能基准目录 ==="
echo ""

if [ -d "benchmarks/baselines" ]; then
    baseline_count=$(find benchmarks/baselines -name "*.json" | wc -l)
    echo -e "${GREEN}✅${NC} 性能基准目录存在"
    echo "   基准文件数: $baseline_count"
    if [ "$baseline_count" -gt 0 ]; then
        echo "   GPU基准:"
        find benchmarks/baselines -name "*.json" -type f | sed 's/^/     - /'
    fi
else
    echo -e "${YELLOW}⚠️${NC}  性能基准目录不存在 (benchmarks/baselines/)"
fi
echo ""

# 8. 最终统计
echo "========================================"
echo "📊 验证统计"
echo "========================================"
echo "总文件数: $total_files"
echo -e "找到文件: ${GREEN}$found_files${NC}"
echo -e "缺失文件: ${RED}$missing_files${NC}"
echo ""

if [ "$missing_files" -eq 0 ]; then
    echo -e "${GREEN}✅ 所有文档完整！${NC}"
    exit 0
else
    echo -e "${YELLOW}⚠️  有 $missing_files 个文件缺失${NC}"
    exit 1
fi

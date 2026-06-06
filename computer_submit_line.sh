#!/bin/bash

# ============================================
# C++ 代码行数统计脚本
# 功能：统计当前Git仓库中C++代码文件的行数
# 支持的文件类型：.cpp, .h, .hpp, .mm
# ============================================

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 检查是否在Git仓库中
if ! git rev-parse --is-inside-work-tree &>/dev/null; then
    echo -e "${RED}错误: 当前目录不是Git仓库${NC}"
    exit 1
fi

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  C++ 代码行数统计${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""

# 获取Git跟踪的C++文件
FILES=$(git ls-files | grep -E '\.(cpp|h|hpp|mm)$')

if [ -z "$FILES" ]; then
    echo -e "${YELLOW}警告: 未找到C++代码文件${NC}"
    exit 0
fi

# 统计文件数量
FILE_COUNT=$(echo "$FILES" | wc -l | tr -d ' ')
echo -e "${BLUE}找到 ${FILE_COUNT} 个C++代码文件${NC}"
echo ""

# 按文件类型分类统计
echo -e "${GREEN}按文件类型统计:${NC}"
echo -e "${YELLOW}----------------------------------------${NC}"

for ext in cpp h hpp mm; do
    COUNT=$(git ls-files | grep -E "\.${ext}$" | wc -l | tr -d ' ')
    if [ "$COUNT" -gt 0 ]; then
        LINES=$(git ls-files | grep -E "\.${ext}$" | xargs cat 2>/dev/null | wc -l | tr -d ' ')
        case $ext in
            cpp)
                echo -e "  ${CYAN}.cpp 文件${NC}: ${COUNT} 个, ${LINES} 行"
                ;;
            h)
                echo -e "  ${CYAN}.h   文件${NC}: ${COUNT} 个, ${LINES} 行"
                ;;
            hpp)
                echo -e "  ${CYAN}.hpp 文件${NC}: ${COUNT} 个, ${LINES} 行"
                ;;
            mm)
                echo -e "  ${CYAN}.mm  文件${NC}: ${COUNT} 个, ${LINES} 行"
                ;;
        esac
    fi
done

echo ""
echo -e "${GREEN}总体统计:${NC}"
echo -e "${YELLOW}----------------------------------------${NC}"

# 显示每个文件的行数（按行数降序排列）
echo -e "${BLUE}详细文件统计（按行数降序）:${NC}"
git ls-files | grep -E '\.(cpp|h|hpp|mm)$' | while read -r file; do
    if [ -f "$file" ]; then
        lines=$(wc -l < "$file" | tr -d ' ')
        printf "  %5d 行  %s\n" "$lines" "$file"
    fi
done | sort -rn

echo ""
echo -e "${YELLOW}----------------------------------------${NC}"

# 总计
TOTAL_LINES=$(git ls-files | grep -E '\.(cpp|h|hpp|mm)$' | xargs cat 2>/dev/null | wc -l | tr -d ' ')
echo -e "${GREEN}代码总行数: ${TOTAL_LINES} 行${NC}"
echo -e "${CYAN}========================================${NC}"

# Git提交统计
echo ""
echo -e "${GREEN}Git提交历史统计:${NC}"
echo -e "${YELLOW}----------------------------------------${NC}"

ADDED=$(git log --all --numstat --pretty=format: -- '*.cpp' '*.h' '*.hpp' '*.mm' | awk '/^[0-9]/ {sum+=$1} END {print sum}')
DELETED=$(git log --all --numstat --pretty=format: -- '*.cpp' '*.h' '*.hpp' '*.mm' | awk '/^[0-9]/ {sum+=$2} END {print sum}')
NET=$((ADDED - DELETED))

echo -e "  历史总增加: ${ADDED} 行"
echo -e "  历史总删除: ${DELETED} 行"
echo -e "  净增行数:   ${NET} 行"
echo -e "${CYAN}========================================${NC}"

echo ""
echo -e "${GREEN}统计完成！${NC}"

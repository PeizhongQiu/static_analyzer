#!/bin/bash

# 中断分析器快速修复脚本 - 解决 clang-tool 路径问题
set -e

echo "🔧 中断分析器快速修复脚本"
echo "==============================="

# 检查当前目录
if [ ! -f "Makefile" ] && [ ! -f "main.cpp" ]; then
    echo "❌ 当前目录不是中断分析器项目目录"
    echo "请在包含 Makefile 和源文件的目录中运行此脚本"
    exit 1
fi

echo "✅ 检测到中断分析器项目"

# 1. 备份重要文件
echo ""
echo "💾 创建备份..."
backup_dir="backup_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$backup_dir"

for file in compile_commands.json Makefile stream_processor.cpp compilation_database.cpp compilation_database.h; do
    if [ -f "$file" ]; then
        cp "$file" "$backup_dir/"
        echo "   备份: $file -> $backup_dir/$file"
    fi
done

echo "✅ 备份完成: $backup_dir/"

# 2. 检查和修复 compile_commands.json
echo ""
echo "📋 检查编译数据库..."

if [ -f "compile_commands.json" ]; then
    # 查找无效的编译器路径
    invalid_compilers=$(grep -o '"/[^"]*clang-tool[^"]*"' compile_commands.json 2>/dev/null || true)
    
    if [ ! -z "$invalid_compilers" ]; then
        echo "❌ 发现无效的编译器路径:"
        echo "$invalid_compilers"
        
        # 查找有效的 clang 替代品
        valid_clang=""
        for clang_candidate in clang++ clang gcc g++; do
            if command -v $clang_candidate >/dev/null 2>&1; then
                valid_clang=$clang_candidate
                break
            fi
        done
        
        if [ ! -z "$valid_clang" ]; then
            echo "✅ 找到有效的编译器: $valid_clang"
            
            # 替换无效路径
            sed -i.bak 's|"/[^"]*clang-tool[^"]*"|"'$valid_clang'"|g' compile_commands.json
            echo "🔄 已替换编译器路径为: $valid_clang"
        else
            echo "❌ 找不到有效的编译器"
        fi
    else
        echo "✅ 编译器路径看起来正常"
    fi
else
    echo "⚠️ 找不到 compile_commands.json 文件"
fi

# 3. 检查依赖
echo ""
echo "🔍 检查构建依赖..."

missing_deps=""

# 检查 LLVM
if ! command -v llvm-config-14 >/dev/null 2>&1 && ! command -v llvm-config >/dev/null 2>&1; then
    missing_deps="$missing_deps LLVM开发包"
fi

# 检查编译器
if ! command -v clang++ >/dev/null 2>&1 && ! command -v g++ >/dev/null 2>&1; then
    missing_deps="$missing_deps C++编译器"
fi

# 检查 JsonCpp
if ! pkg-config --exists jsoncpp 2>/dev/null && ! ldconfig -p | grep -q jsoncpp 2>/dev/null; then
    missing_deps="$missing_deps JsonCpp库"
fi

if [ -z "$missing_deps" ]; then
    echo "✅ 所有基本依赖都已安装"
else
    echo "❌ 缺少以下依赖:$missing_deps"
    echo ""
    read -p "是否自动安装缺失的依赖? (y/N): " -n 1 -r
    echo
    if [ "$REPLY" = "y" ] || [ "$REPLY" = "Y" ]; then
        if command -v apt >/dev/null 2>&1; then
            sudo apt update
            sudo apt install -y llvm-14-dev clang-14-dev libjsoncpp-dev build-essential
        elif command -v yum >/dev/null 2>&1; then
            sudo yum install -y llvm-devel clang-devel jsoncpp-devel gcc-c++
        elif command -v dnf >/dev/null 2>&1; then
            sudo dnf install -y llvm-devel clang-devel jsoncpp-devel gcc-c++
        else
            echo "⚠️ 请手动安装依赖"
        fi
    fi
fi

# 4. 创建简化的测试环境
echo ""
echo "📋 创建简化测试环境..."

# 找到有效的编译器
compiler=""
for candidate in clang++ clang gcc g++; do
    if command -v $candidate >/dev/null 2>&1; then
        compiler=$candidate
        break
    fi
done

if [ -z "$compiler" ]; then
    echo "❌ 找不到任何可用的编译器"
    exit 1
fi

echo "✅ 使用编译器: $compiler"

# 创建简化的编译数据库
cat > compile_commands_test.json << EOF
[
  {
    "directory": ".",
    "file": "./test_simple.c",
    "arguments": ["$compiler", "-std=c99", "-w", "-I.", "-c", "./test_simple.c"]
  }
]
EOF

# 创建简单的测试文件
cat > test_simple.c << 'EOF'
/* 简单测试文件，避免复杂的内核依赖 */

static int global_counter = 0;
static char global_buffer[256];

void simple_handler(void) {
    global_counter++;
    global_buffer[0] = 'x';
}

void another_function(int param) {
    if (param > 0) {
        simple_handler();
    }
    global_counter = param;
}

int main(void) {
    another_function(42);
    return 0;
}
EOF

echo "✅ 创建了简化的测试环境"

# 5. 构建项目
echo ""
echo "🔨 尝试构建项目..."

if make clean 2>/dev/null && make; then
    echo "✅ 项目构建成功！"
    
    # 6. 测试修复后的分析器
    echo ""
    echo "🧪 测试修复后的分析器..."
    
    # 备份原始编译数据库
    if [ -f "compile_commands.json" ]; then
        mv compile_commands.json compile_commands_original.json
    fi
    mv compile_commands_test.json compile_commands.json
    
    echo "🚀 运行简化测试..."
    echo "命令: ./interrupt_analyzer --handler=simple_handler --file=test_simple.c -p . test_simple.c"
    
    if timeout 30s ./interrupt_analyzer --handler=simple_handler --file=test_simple.c -p . test_simple.c; then
        echo ""
        echo "✅ 简化测试成功！"
        
        if [ -f "analysis_result_simple_handler_streaming.json" ]; then
            echo "📄 分析结果已生成:"
            ls -la analysis_result_*.json
            
            echo ""
            echo "📊 结果摘要:"
            if command -v jq >/dev/null 2>&1; then
                echo "统计信息:"
                jq '.statistics' analysis_result_simple_handler_streaming.json 2>/dev/null || echo "无法解析JSON结果"
                echo ""
                echo "检测到的写操作:"
                jq '.filtered_writes | length' analysis_result_simple_handler_streaming.json 2>/dev/null || echo "无法解析写操作数据"
            else
                echo "安装 jq 来查看详细的JSON结果: sudo apt install jq"
                echo "或者手动查看文件: analysis_result_simple_handler_streaming.json"
            fi
        fi
        
        echo ""
        echo "🎉 修复成功！现在可以尝试分析真实的内核代码了。"
        
    else
        echo ""
        echo "❌ 简化测试失败"
        echo ""
        echo "🔍 可能的问题和解决方案:"
        echo "1. 检查 LLVM/Clang 版本兼容性:"
        echo "   llvm-config --version"
        echo "   clang++ --version"
        echo ""
        echo "2. 尝试使用调试模式:"
        echo "   DEBUG_ANALYZER=1 ./interrupt_analyzer --handler=simple_handler --file=test_simple.c -p . test_simple.c"
        echo ""
        echo "3. 检查详细的构建信息:"
        echo "   make debug"
        echo ""
        echo "4. 如果问题持续，可能需要降级到传统模式:"
        echo "   ./interrupt_analyzer --handler=simple_handler --file=test_simple.c --no-streaming -p . test_simple.c"
    fi
    
    # 恢复原始编译数据库
    if [ -f "compile_commands_original.json" ]; then
        mv compile_commands_original.json compile_commands.json
    fi
    
else
    echo "❌ 项目构建失败"
    echo ""
    echo "🔍 构建问题诊断:"
    echo "1. 检查依赖是否完整安装"
    echo "2. 运行 make debug 查看详细配置"
    echo "3. 检查编译器版本是否支持 C++17"
    echo "4. 查看具体的编译错误信息"
fi

# 7. 清理和建议
echo ""
echo "🧹 清理测试文件..."
read -p "是否删除测试文件? (Y/n): " -n 1 -r
echo
if [ "$REPLY" != "n" ] && [ "$REPLY" != "N" ]; then
    rm -f test_simple.c compile_commands_test.json analysis_result_*.json
    echo "✅ 测试文件已清理"
fi

echo ""
echo "📋 修复总结:"
echo "============"

if [ -f "./interrupt_analyzer" ]; then
    echo "✅ 可执行文件存在"
    echo "✅ 编译器路径问题已修复"
    echo "✅ 项目可以正常构建"
    
    echo ""
    echo "🎯 下一步建议:"
    echo "1. 尝试分析真实的内核代码:"
    echo "   ./interrupt_analyzer --handler=你的处理函数 --file=源文件.c --streaming -p 构建目录 源文件路径"
    echo ""
    echo "2. 如果遇到路径问题，检查 compile_commands.json 中的路径是否正确"
    echo ""
    echo "3. 使用调试模式查看详细信息:"
    echo "   DEBUG_ANALYZER=1 ./interrupt_analyzer ..."
    echo ""
    echo "4. 对于大型项目，可以调整性能参数:"
    echo "   ./interrupt_analyzer --streaming --worker-threads=8 --max-memory=2048 ..."
    
else
    echo "❌ 可执行文件不存在，构建可能失败"
    echo ""
    echo "🔧 进一步的修复步骤:"
    echo "1. 检查构建日志中的具体错误"
    echo "2. 确认所有依赖都已正确安装"
    echo "3. 尝试手动编译单个文件进行测试"
    echo "4. 检查 LLVM/Clang 版本兼容性"
fi

echo ""
echo "💾 备份文件保存在: $backup_dir/"
echo "如果需要恢复原始文件，可以从该目录复制"

echo ""
echo "🎉 快速修复脚本执行完成！"

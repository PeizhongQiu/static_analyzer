TARGET_FIXED = interrupt_analyzer_fixed
CXX = clang++
CXXFLAGS = -std=c++17 -Wall -O2 -fno-exceptions

# 源文件列表
SOURCES = interrupt_analyzer.cpp \
          stream_processor.cpp \
          analysis_data.cpp \
          ast_visitor_base.cpp \
          ast_visitor.cpp \
          pointer_analysis.cpp \
          write_analysis.cpp \
          function_pointer_analysis.cpp \
          assembly_analysis.cpp \
          compilation_database.cpp \
          clang_frontend.cpp \
          cache_manager.cpp

OBJECTS = $(SOURCES:.cpp=.o)
MAIN_OBJ = main.o

# LLVM 配置 - 简化版本
LLVM_CXXFLAGS = $(shell llvm-config-14 --cxxflags 2>/dev/null | sed 's/-std=c++[0-9]*//' || llvm-config --cxxflags 2>/dev/null | sed 's/-std=c++[0-9]*//' || echo "-I/usr/include")
LLVM_LDFLAGS = $(shell llvm-config-14 --ldflags 2>/dev/null || llvm-config --ldflags 2>/dev/null)
LLVM_LIBS = $(shell llvm-config-14 --libs 2>/dev/null || llvm-config --libs 2>/dev/null)
LLVM_SYSLIBS = $(shell llvm-config-14 --system-libs 2>/dev/null || llvm-config --system-libs 2>/dev/null)

# Clang 库 - 按依赖顺序
CLANG_LIBS = -lclangTooling -lclangFrontend -lclangDriver -lclangSerialization \
             -lclangCodeGen -lclangParse -lclangSema -lclangAnalysis \
             -lclangRewrite -lclangEdit -lclangAST -lclangLex -lclangBasic

# 系统库
SYS_LIBS = -ljsoncpp -lpthread -ldl -lz -lm

# 默认目标
all: $(TARGET_FIXED)

# 构建修复版本
$(TARGET_FIXED): $(OBJECTS) $(MAIN_OBJ)
	@echo "🔗 链接修复版本..."
	$(CXX) -o $@ $^ $(LLVM_LDFLAGS) $(CLANG_LIBS) $(LLVM_LIBS) $(LLVM_SYSLIBS) $(SYS_LIBS)
	@echo "✅ 构建完成: $(TARGET_FIXED)"

# 编译主程序
$(MAIN_OBJ): main.cpp
	@echo "🔄 编译主程序..."
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -c $< -o $@

# 通用编译规则
%.o: %.cpp
	@echo "🔄 编译 $<..."
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -c $< -o $@

# 清理
clean:
	@echo "🧹 清理..."
	rm -f $(OBJECTS) $(MAIN_OBJ) $(TARGET_FIXED) *.json
	@echo "✅ 清理完成"

# 快速测试
test: $(TARGET_FIXED)
	@echo "🧪 运行测试..."
	@if [ -f "compile_commands.json" ]; then \
		./$(TARGET_FIXED) --handler=vm_interrupt --file=vm.c \
			--worker-threads=4 --max-memory=512 --batch-size=8 \
			--debug -p .; \
	else \
		echo "⚠️ 请确保 compile_commands.json 存在"; \
	fi

# 检查环境
check:
	@echo "🔍 检查编译环境..."
	@echo "CXX: $(CXX)"
	@which $(CXX) >/dev/null 2>&1 && echo "✅ 编译器可用" || echo "❌ 编译器未找到"
	@llvm-config-14 --version 2>/dev/null && echo "✅ LLVM-14 可用" || \
	 llvm-config --version 2>/dev/null && echo "✅ LLVM 可用" || \
	 echo "❌ LLVM 未找到"
	@pkg-config --exists jsoncpp && echo "✅ jsoncpp 可用" || echo "❌ jsoncpp 未找到"
	@[ -f "compile_commands.json" ] && echo "✅ 编译数据库存在" || echo "⚠️ 编译数据库缺失"

# 显示帮助
help:
	@echo "🚀 中断分析器构建帮助"
	@echo "======================"
	@echo "make          - 构建程序"
	@echo "make clean    - 清理文件"
	@echo "make test     - 运行测试"
	@echo "make check    - 检查环境"
	@echo "make help     - 显示帮助"

.PHONY: all clean test check help

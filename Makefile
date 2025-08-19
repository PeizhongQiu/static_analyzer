TARGET = interrupt_analyzer
CXX = clang++
CXXFLAGS = -std=c++17 -Wall -O2 -fno-rtti

# 重构后的源文件
SOURCES = main.cpp \
          interrupt_analyzer.cpp \
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

# LLVM 配置 - 分离编译和链接标志
LLVM_CXXFLAGS = $(shell llvm-config-14 --cxxflags 2>/dev/null | sed 's/-std=c++[0-9]*//' || llvm-config --cxxflags 2>/dev/null | sed 's/-std=c++[0-9]*//' || echo "-I/usr/include")
LLVM_LDFLAGS = $(shell llvm-config-14 --ldflags 2>/dev/null || llvm-config --ldflags 2>/dev/null)
LLVM_LIBS = $(shell llvm-config-14 --libs 2>/dev/null || llvm-config --libs 2>/dev/null)
LLVM_SYSLIBS = $(shell llvm-config-14 --system-libs 2>/dev/null || llvm-config --system-libs 2>/dev/null)

# Clang 库 - 按正确的依赖顺序排列
CLANG_LIBS = -lclangTooling \
             -lclangFrontendTool \
             -lclangFrontend \
             -lclangDriver \
             -lclangSerialization \
             -lclangCodeGen \
             -lclangParse \
             -lclangSema \
             -lclangStaticAnalyzerFrontend \
             -lclangStaticAnalyzerCheckers \
             -lclangStaticAnalyzerCore \
             -lclangAnalysis \
             -lclangARCMigrate \
             -lclangRewrite \
             -lclangRewriteFrontend \
             -lclangEdit \
             -lclangAST \
             -lclangLex \
             -lclangBasic

# 系统库
SYS_LIBS = -ljsoncpp -lpthread -ldl -lz -lm

# 默认目标
all: $(TARGET)

# 链接目标 - 确保库的顺序正确
$(TARGET): $(OBJECTS)
	@echo "🔗 Linking $(TARGET)..."
	$(CXX) -o $@ $^ $(LLVM_LDFLAGS) $(CLANG_LIBS) $(LLVM_LIBS) $(LLVM_SYSLIBS) $(SYS_LIBS)
	@echo "✅ Build completed successfully!"

# 编译对象文件
%.o: %.cpp
	@echo "🔄 Compiling $<..."
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -c $< -o $@

# 清理
clean:
	@echo "🧹 Cleaning build artifacts..."
	rm -f $(OBJECTS) $(TARGET) analysis_cache.json
	@echo "✅ Clean completed!"

# 深度清理（包括缓存）
clean-all: clean
	@echo "🧹 Cleaning all generated files..."
	rm -f *.json analysis_result_*.json
	@echo "✅ Deep clean completed!"

# 安装
install: $(TARGET)
	@echo "📦 Installing $(TARGET)..."
	cp $(TARGET) /usr/local/bin/
	@echo "✅ Installation completed!"

# 调试信息 - 显示配置
debug:
	@echo "🔍 Build Configuration:"
	@echo "SOURCES: $(SOURCES)"
	@echo "OBJECTS: $(OBJECTS)"
	@echo "LLVM_CXXFLAGS: $(LLVM_CXXFLAGS)"
	@echo "LLVM_LDFLAGS: $(LLVM_LDFLAGS)"  
	@echo "LLVM_LIBS: $(LLVM_LIBS)"
	@echo "LLVM_SYSLIBS: $(LLVM_SYSLIBS)"
	@echo "CLANG_LIBS: $(CLANG_LIBS)"

# 检查依赖
check-deps:
	@echo "🔍 Checking dependencies..."
	@which llvm-config-14 >/dev/null 2>&1 || which llvm-config >/dev/null 2>&1 || echo "⚠️ WARNING: llvm-config not found"
	@pkg-config --exists jsoncpp 2>/dev/null || echo "⚠️ WARNING: jsoncpp not found"
	@echo "✅ Dependencies check completed"

# 测试目标
test: $(TARGET)
	@echo "🧪 Running basic functionality test..."
	@echo "Note: This requires a compile_commands.json file in the current directory"
	@if [ -f "compile_commands.json" ]; then \
		echo "✅ Found compile_commands.json"; \
	else \
		echo "⚠️ No compile_commands.json found in current directory"; \
	fi

# 清理缓存
clean-cache:
	@echo "🗑️ Cleaning analysis cache..."
	rm -f analysis_cache.json
	@echo "✅ Cache cleaned!"

# 显示模块结构
show-modules:
	@echo "📋 Module Structure:"
	@echo "  🏗️ Core:"
	@echo "    - analysis_data: Data storage and management"
	@echo "    - ast_visitor_base: Base AST visitor functionality"
	@echo "  🔍 Analysis Modules:"
	@echo "    - pointer_analysis: Pointer alias and parameter tracking"
	@echo "    - write_analysis: Global variable write detection"
	@echo "    - function_pointer_analysis: Function pointer handling"
	@echo "    - assembly_analysis: Inline assembly processing"
	@echo "  🏭 Infrastructure:"
	@echo "    - compilation_database: Build system integration"
	@echo "    - clang_frontend: Clang tooling management"
	@echo "    - cache_manager: Result caching system"
	@echo "  🎯 Main:"
	@echo "    - interrupt_analyzer: Main analysis coordinator"
	@echo "    - main: CLI interface"

# 模块化构建（可选）
build-modules: 
	@echo "🔧 Building core modules first..."
	$(MAKE) analysis_data.o ast_visitor_base.o
	@echo "🔧 Building analysis modules..."
	$(MAKE) pointer_analysis.o write_analysis.o function_pointer_analysis.o assembly_analysis.o
	@echo "🔧 Building infrastructure modules..."
	$(MAKE) compilation_database.o clang_frontend.o cache_manager.o
	@echo "🔧 Building main components..."
	$(MAKE) interrupt_analyzer.o main.o
	@echo "🔗 Linking final executable..."
	$(MAKE) $(TARGET)

.PHONY: all clean clean-all clean-cache install debug check-deps test show-modules build-modules

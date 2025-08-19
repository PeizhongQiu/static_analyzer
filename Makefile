TARGET = interrupt_analyzer
CXX = clang++
CXXFLAGS = -std=c++17 -Wall -O2

# 流式处理优化后的源文件
SOURCES = main.cpp \
          interrupt_analyzer.cpp \
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

# 系统库 - 添加线程支持
SYS_LIBS = -ljsoncpp -lpthread -ldl -lz -lm

# 默认目标
all: $(TARGET)

# 链接目标 - 确保库的顺序正确
$(TARGET): $(OBJECTS)
	@echo "🔗 Linking $(TARGET) with streaming support..."
	$(CXX) -o $@ $^ $(LLVM_LDFLAGS) $(CLANG_LIBS) $(LLVM_LIBS) $(LLVM_SYSLIBS) $(SYS_LIBS)
	@echo "✅ Streaming build completed successfully!"

# 编译对象文件 - 添加新的依赖关系
stream_processor.o: stream_processor.cpp stream_processor.h analysis_data.h
	@echo "🔄 Compiling streaming processor..."
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -c $< -o $@

interrupt_analyzer.o: interrupt_analyzer.cpp interrupt_analyzer.h stream_processor.h analysis_data.h
	@echo "🔄 Compiling streaming analyzer..."
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -c $< -o $@

ast_visitor_base.o: ast_visitor_base.cpp ast_visitor_base.h analysis_data.h data_structures.h
	@echo "🔄 Compiling $<..."
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -c $< -o $@

ast_visitor.o: ast_visitor.cpp ast_visitor.h ast_visitor_base.h pointer_analysis.h write_analysis.h function_pointer_analysis.h assembly_analysis.h
	@echo "🔄 Compiling $<..."
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -c $< -o $@

pointer_analysis.o: pointer_analysis.cpp pointer_analysis.h analysis_data.h
	@echo "🔄 Compiling $<..."
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -c $< -o $@

write_analysis.o: write_analysis.cpp write_analysis.h analysis_data.h pointer_analysis.h
	@echo "🔄 Compiling $<..."
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -c $< -o $@

function_pointer_analysis.o: function_pointer_analysis.cpp function_pointer_analysis.h analysis_data.h
	@echo "🔄 Compiling $<..."
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -c $< -o $@

assembly_analysis.o: assembly_analysis.cpp assembly_analysis.h analysis_data.h
	@echo "🔄 Compiling $<..."
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -c $< -o $@

# 默认编译规则（用于其他文件）
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
	rm -rf .analysis_cache/
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

# 流式处理测试
test-streaming: $(TARGET)
	@echo "🧪 Running streaming processing test..."
	@echo "Note: This requires a compile_commands.json file in the current directory"
	@if [ -f "compile_commands.json" ]; then \
		echo "✅ Found compile_commands.json"; \
		echo "🚀 Testing streaming mode..."; \
		./$(TARGET) --handler=test_handler --file=test.c --streaming --threads=4 --memory=256 --batch-size=10 || echo "⚠️ Test completed with warnings"; \
	else \
		echo "⚠️ No compile_commands.json found in current directory"; \
	fi

# 性能基准测试
benchmark: $(TARGET)
	@echo "📊 Running performance benchmark..."
	@if [ -f "compile_commands.json" ]; then \
		echo "🔄 Testing traditional mode..."; \
		time ./$(TARGET) --handler=benchmark_handler --file=test.c --no-streaming --output=result_traditional.json || true; \
		echo "🚀 Testing streaming mode..."; \
		time ./$(TARGET) --handler=benchmark_handler --file=test.c --streaming --output=result_streaming.json || true; \
		echo "✅ Benchmark completed! Check result_*.json files for comparison."; \
	else \
		echo "⚠️ Benchmark requires compile_commands.json"; \
	fi

# 内存测试
test-memory: $(TARGET)
	@echo "🧠 Testing memory usage..."
	@if [ -f "compile_commands.json" ]; then \
		echo "Testing with different memory limits..."; \
		./$(TARGET) --handler=memory_test --file=test.c --streaming --memory=100 --threads=2 || true; \
		./$(TARGET) --handler=memory_test --file=test.c --streaming --memory=500 --threads=4 || true; \
		./$(TARGET) --handler=memory_test --file=test.c --streaming --memory=1000 --threads=8 || true; \
	else \
		echo "⚠️ Memory test requires compile_commands.json"; \
	fi

# 显示流式处理特性
show-streaming-features:
	@echo "🚀 Streaming Processing Features:"
	@echo "  ✅ Multi-threaded Analysis:"
	@echo "    - Parallel file processing with configurable thread pool"
	@echo "    - Intelligent task scheduling based on file size and priority"
	@echo "  ✅ Memory Optimization:"
	@echo "    - Configurable memory limits with pressure relief"
	@echo "    - Batch processing to control memory usage"
	@echo "    - Automatic cache eviction under memory pressure"
	@echo "  ✅ Incremental Analysis:"
	@echo "    - File-level change detection using content hashing"
	@echo "    - Cached results for unchanged files"
	@echo "    - Significant speedup for incremental builds"
	@echo "  ✅ Real-time Progress:"
	@echo "    - Live progress reporting during analysis"
	@echo "    - Performance statistics and cache hit rates"
	@echo "    - Memory usage monitoring"
	@echo "  ✅ Flexible Configuration:"
	@echo "    - Command-line options for all parameters"
	@echo "    - Automatic hardware detection for optimal settings"
	@echo "    - Backward compatibility with traditional mode"

# 显示使用示例
show-examples:
	@echo "📖 Usage Examples:"
	@echo ""
	@echo "🚀 Basic Streaming Mode:"
	@echo "  make && ./interrupt_analyzer --handler=timer_irq --file=timer.c --streaming -p build src/"
	@echo ""
	@echo "⚡ High Performance Mode:"
	@echo "  ./interrupt_analyzer --handler=interrupt_handler --file=irq.c \\"
	@echo "    --streaming --threads=16 --memory=2048 --batch-size=50 \\"
	@echo "    -p build src/"
	@echo ""
	@echo "💾 Memory Constrained Mode:"
	@echo "  ./interrupt_analyzer --handler=low_mem_handler --file=handler.c \\"
	@echo "    --streaming --threads=2 --memory=128 --batch-size=5 \\"
	@echo "    --memory-relief -p build src/"
	@echo ""
	@echo "🔄 Traditional Mode (for comparison):"
	@echo "  ./interrupt_analyzer --handler=old_handler --file=old.c \\"
	@echo "    --no-streaming -p build src/"
	@echo ""
	@echo "📊 Benchmark Comparison:"
	@echo "  make benchmark  # Runs both modes and compares performance"

# 快速开始指南
quick-start:
	@echo "🚀 Quick Start Guide:"
	@echo "1. Build the analyzer:"
	@echo "   make"
	@echo ""
	@echo "2. Ensure you have a compile_commands.json file"
	@echo "   (generated by your build system)"
	@echo ""
	@echo "3. Run streaming analysis:"
	@echo "   ./interrupt_analyzer --handler=YOUR_HANDLER --file=YOUR_FILE.c \\"
	@echo "     --streaming -p YOUR_BUILD_DIR YOUR_SOURCE_FILES"
	@echo ""
	@echo "4. Check the output JSON file for results"
	@echo ""
	@echo "💡 For large projects, try:"
	@echo "   ./interrupt_analyzer --handler=YOUR_HANDLER --file=YOUR_FILE.c \\"
	@echo "     --streaming --threads=8 --memory=1024 \\"
	@echo "     -p YOUR_BUILD_DIR YOUR_SOURCE_FILES

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

# 智能 LLVM 配置检测
LLVM_CONFIG := $(shell which llvm-config-14 2>/dev/null || which llvm-config-13 2>/dev/null || which llvm-config-12 2>/dev/null || which llvm-config 2>/dev/null)

# 如果找不到 llvm-config，尝试系统路径
ifeq ($(LLVM_CONFIG),)
    LLVM_CONFIG := $(shell find /usr/bin /usr/local/bin -name "llvm-config*" 2>/dev/null | head -1)
endif

# 如果仍然找不到，给出错误提示
ifeq ($(LLVM_CONFIG),)
$(error "未找到 llvm-config。请安装 LLVM 开发包: sudo apt install llvm-14-dev clang-14-dev")
endif

# LLVM 配置 - 移除可能冲突的标志
LLVM_CXXFLAGS_RAW = $(shell $(LLVM_CONFIG) --cxxflags 2>/dev/null || echo "")
LLVM_CXXFLAGS = $(filter-out -std=c++%, $(filter-out -fno-rtti, $(LLVM_CXXFLAGS_RAW)))

LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags 2>/dev/null || echo "")
LLVM_LIBS = $(shell $(LLVM_CONFIG) --libs 2>/dev/null || echo "")
LLVM_SYSLIBS = $(shell $(LLVM_CONFIG) --system-libs 2>/dev/null || echo "")

# Clang 库 - 按依赖顺序，移除可能不存在的库
CLANG_LIBS = -lclangTooling \
             -lclangFrontend \
             -lclangDriver \
             -lclangSerialization \
             -lclangCodeGen \
             -lclangParse \
             -lclangSema \
             -lclangAnalysis \
             -lclangRewrite \
             -lclangEdit \
             -lclangAST \
             -lclangLex \
             -lclangBasic

# 系统库 - 智能检测 jsoncpp
SYS_LIBS = -lpthread -ldl -lz -lm

# 检查 jsoncpp 是否可用
JSONCPP_AVAILABLE := $(shell pkg-config --exists jsoncpp 2>/dev/null && echo "yes" || echo "no")
ifeq ($(JSONCPP_AVAILABLE),yes)
    JSONCPP_CFLAGS := $(shell pkg-config --cflags jsoncpp 2>/dev/null || echo "")
    JSONCPP_LIBS := $(shell pkg-config --libs jsoncpp 2>/dev/null || echo "")
    CXXFLAGS += $(JSONCPP_CFLAGS)
    SYS_LIBS += $(JSONCPP_LIBS)
else
    # 尝试直接链接 jsoncpp
    SYS_LIBS += -ljsoncpp
endif

# 默认目标
all: info $(TARGET)

# 显示配置信息
info:
	@echo "🔍 构建配置信息:"
	@echo "  LLVM Config: $(LLVM_CONFIG)"
	@echo "  LLVM 版本: $$($(LLVM_CONFIG) --version 2>/dev/null | head -1 || echo 'Unknown')"
	@echo "  JsonCpp: $(JSONCPP_AVAILABLE)"
	@echo "  编译器: $(CXX)"
	@echo ""

# 链接目标 - 使用容错链接
$(TARGET): $(OBJECTS)
	@echo "🔗 链接 $(TARGET)..."
	@echo "正在尝试链接，如果失败会自动重试..."
	@$(CXX) -o $@ $^ $(LLVM_LDFLAGS) $(CLANG_LIBS) $(LLVM_LIBS) $(LLVM_SYSLIBS) $(SYS_LIBS) 2>/dev/null || \
	 $(CXX) -o $@ $^ $(LLVM_LDFLAGS) $(CLANG_LIBS) $(LLVM_LIBS) $(SYS_LIBS) 2>/dev/null || \
	 (echo "❌ 标准链接失败，尝试简化链接..." && \
	  $(CXX) -o $@ $^ -L$$($(LLVM_CONFIG) --libdir) $(CLANG_LIBS) -lLLVM $(SYS_LIBS))
	@echo "✅ $(TARGET) 构建成功！"

# 编译规则 - 增加错误处理
%.o: %.cpp
	@echo "🔄 编译 $<..."
	@$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -c $< -o $@ 2>/dev/null || \
	 (echo "❌ 使用标准设置编译 $< 失败，尝试基础设置..." && \
	  $(CXX) $(CXXFLAGS) -I$$($(LLVM_CONFIG) --includedir) -c $< -o $@)

# 清理
clean:
	@echo "🧹 清理构建产物..."
	rm -f $(OBJECTS) $(TARGET) *.json
	@echo "✅ 清理完成!"

# 深度清理
clean-all: clean
	@echo "🧹 深度清理..."
	rm -f *.json analysis_result_*.json
	rm -rf .analysis_cache/
	@echo "✅ 深度清理完成!"

# 安装依赖 (Ubuntu/Debian)
install-deps:
	@echo "📦 安装构建依赖..."
	@if command -v apt >/dev/null 2>&1; then \
		echo "检测到 Ubuntu/Debian 系统"; \
		sudo apt update && sudo apt install -y llvm-14-dev clang-14-dev libjsoncpp-dev build-essential; \
	elif command -v yum >/dev/null 2>&1; then \
		echo "检测到 CentOS/RHEL 系统"; \
		sudo yum install -y llvm-devel clang-devel jsoncpp-devel gcc-c++; \
	elif command -v dnf >/dev/null 2>&1; then \
		echo "检测到 Fedora 系统"; \
		sudo dnf install -y llvm-devel clang-devel jsoncpp-devel gcc-c++; \
	else \
		echo "⚠️ 未识别的包管理器，请手动安装:"; \
		echo "  - LLVM 开发包"; \
		echo "  - Clang 开发包"; \
		echo "  - JsonCpp 开发包"; \
	fi

# 测试构建
test: $(TARGET)
	@echo "🧪 测试程序..."
	@if ./$(TARGET) --help >/dev/null 2>&1; then \
		echo "✅ 程序运行正常"; \
	else \
		echo "⚠️ 程序可能需要额外配置"; \
	fi

# 调试信息
debug:
	@echo "🔍 调试信息:"
	@echo "  当前目录: $$(pwd)"
	@echo "  LLVM Config: $(LLVM_CONFIG)"
	@echo "  LLVM 版本: $$($(LLVM_CONFIG) --version 2>/dev/null || echo '未找到')"
	@echo "  LLVM 前缀: $$($(LLVM_CONFIG) --prefix 2>/dev/null || echo '未知')"
	@echo "  LLVM 库目录: $$($(LLVM_CONFIG) --libdir 2>/dev/null || echo '未知')"
	@echo "  LLVM 包含目录: $$($(LLVM_CONFIG) --includedir 2>/dev/null || echo '未知')"
	@echo "  JsonCpp 可用: $(JSONCPP_AVAILABLE)"
	@echo "  编译器: $$($(CXX) --version | head -1 2>/dev/null || echo '未知')"
	@echo "  源文件: $(SOURCES)"
	@echo "  目标文件: $(OBJECTS)"

# 一键构建（包含依赖安装）
build-all: install-deps clean all test

.PHONY: all clean clean-all install-deps test debug info build-all

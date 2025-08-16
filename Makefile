TARGET = interrupt_analyzer
CXX = clang++
CXXFLAGS = -std=c++17 -Wall -O2 -fno-rtti

# 源文件
SOURCES = main.cpp interrupt_analyzer.cpp ast_visitor.cpp analysis_data.cpp
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
	$(CXX) -o $@ $^ $(LLVM_LDFLAGS) $(CLANG_LIBS) $(LLVM_LIBS) $(LLVM_SYSLIBS) $(SYS_LIBS)

# 编译对象文件
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -c $< -o $@

# 清理
clean:
	rm -f $(OBJECTS) $(TARGET)

# 安装
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

# 调试信息 - 显示配置
debug:
	@echo "LLVM_CXXFLAGS: $(LLVM_CXXFLAGS)"
	@echo "LLVM_LDFLAGS: $(LLVM_LDFLAGS)"  
	@echo "LLVM_LIBS: $(LLVM_LIBS)"
	@echo "LLVM_SYSLIBS: $(LLVM_SYSLIBS)"
	@echo "CLANG_LIBS: $(CLANG_LIBS)"

# 检查依赖
check-deps:
	@echo "Checking LLVM/Clang installation..."
	@which llvm-config-14 >/dev/null 2>&1 || which llvm-config >/dev/null 2>&1 || echo "WARNING: llvm-config not found"
	@pkg-config --exists jsoncpp 2>/dev/null || echo "WARNING: jsoncpp not found"
	@echo "Dependencies check completed"

.PHONY: all clean install debug check-deps

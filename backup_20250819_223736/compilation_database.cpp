#include "compilation_database.h"
#include <llvm/Support/Path.h>
#include <llvm/Support/FileSystem.h>
#include <jsoncpp/json/json.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <iostream>

CompilationDatabaseProcessor::CompilationDatabaseProcessor(const std::string& database_path)
    : db_path(database_path) {}

//=============================================================================
// 加载和处理
//=============================================================================

bool CompilationDatabaseProcessor::loadCompileCommands() {
    if (!llvm::sys::fs::exists(db_path)) {
        std::cout << "❌ 找不到编译数据库: " << db_path << std::endl;
        return false;
    }

    std::ifstream file(db_path);
    if (!file.is_open()) {
        std::cout << "❌ 无法打开编译数据库" << std::endl;
        return false;
    }

    Json::Value root;
    Json::Reader reader;

    if (!reader.parse(file, root) || !root.isArray()) {
        std::cout << "❌ 解析编译数据库失败" << std::endl;
        return false;
    }

    compile_commands.clear();

    for (Json::ArrayIndex i = 0; i < root.size(); ++i) {
        const Json::Value& entry = root[i];
        if (!entry.isObject()) continue;

        LocalCompileCommand cmd;

        if (!entry.isMember("directory") || !entry.isMember("file")) continue;

        cmd.directory = entry["directory"].asString();
        cmd.file = entry["file"].asString();

        // 智能路径处理
        if (!processFilePath(cmd)) {
            continue; // 跳过无法处理的路径
        }

        if (!isCppSourceFile(cmd.file)) continue;

        // 读取编译参数并验证编译器
        if (entry.isMember("arguments") && entry["arguments"].isArray()) {
            for (const auto& arg : entry["arguments"]) {
                if (arg.isString()) {
                    cmd.arguments.push_back(arg.asString());
                }
            }
            
            // 验证并修复编译器路径
            if (!cmd.arguments.empty()) {
                cmd.arguments[0] = validateCompilerPath(cmd.arguments[0]);
            }
        } else if (entry.isMember("command") && entry["command"].isString()) {
            std::string command = entry["command"].asString();
            std::istringstream iss(command);
            std::string arg;
            while (iss >> std::quoted(arg)) {
                cmd.arguments.push_back(arg);
            }
            
            // 验证并修复编译器路径
            if (!cmd.arguments.empty()) {
                cmd.arguments[0] = validateCompilerPath(cmd.arguments[0]);
            }
        } else {
            continue;
        }

        compile_commands.push_back(std::move(cmd));
    }

    std::cout << "✅ 加载了 " << compile_commands.size() << " 个源文件的编译信息" << std::endl;
    return !compile_commands.empty();
}

bool CompilationDatabaseProcessor::processFilePath(LocalCompileCommand& cmd) {
    std::string original_file = cmd.file;
    
    // 处理相对路径中的 "./" 前缀
    if (cmd.file.substr(0, 2) == "./") {
        cmd.file = cmd.file.substr(2);
    }

    // 转换为绝对路径
    if (!llvm::sys::path::is_absolute(cmd.file)) {
        llvm::SmallString<256> absolute_path(cmd.directory);
        llvm::sys::path::append(absolute_path, cmd.file);
        cmd.file = absolute_path.str().str();
    }

    // 尝试规范化路径
    llvm::SmallString<256> canonical_path;
    if (!llvm::sys::fs::real_path(cmd.file, canonical_path)) {
        cmd.file = canonical_path.str().str();
        return true;
    }

    // 如果规范化失败，检查文件是否存在
    if (llvm::sys::fs::exists(cmd.file)) {
        return true; // 文件存在，使用原路径
    }

    // 文件不存在，尝试不同的路径策略
    return tryAlternativePaths(cmd, original_file);
}

bool CompilationDatabaseProcessor::tryAlternativePaths(LocalCompileCommand& cmd, const std::string& original_file) {
    // 策略1: 如果是绝对路径，尝试转换为相对路径
    if (llvm::sys::path::is_absolute(original_file)) {
        // 查找常见的项目根目录标识
        llvm::StringRef path_ref(original_file);
        
        std::vector<std::string> project_markers = {"kafl.linux", "linux", "kernel", "src", "source"};
        
        for (const auto& marker : project_markers) {
            size_t pos = path_ref.find(marker);
            if (pos != llvm::StringRef::npos) {
                std::string relative_part = path_ref.substr(pos).str();
                
                // 尝试不同的基础路径
                std::vector<std::string> base_paths = {
                    "../",
                    "../../",
                    "./",
                    std::string("../") + marker + "/"
                };
                
                for (const auto& base : base_paths) {
                    std::string candidate = base + relative_part;
                    if (llvm::sys::fs::exists(candidate)) {
                        cmd.file = candidate;
                        return true;
                    }
                }
            }
        }
    }

    // 策略2: 尝试在当前目录和父目录中查找
    std::string filename = llvm::sys::path::filename(original_file).str();
    std::vector<std::string> search_paths = {
        cmd.directory + "/" + filename,
        "../" + filename,
        "../../" + filename,
        "./" + filename
    };

    for (const auto& candidate : search_paths) {
        if (llvm::sys::fs::exists(candidate)) {
            cmd.file = candidate;
            return true;
        }
    }

    // 策略3: 使用原始路径，但标记为可能有问题
    static int warning_count = 0;
    if (warning_count < 10) { // 只显示前10个警告
        std::cout << "⚠️ 文件路径可能有问题: " << original_file << std::endl;
        warning_count++;
    }
    
    return false; // 跳过这个文件
}

// 新增方法：验证和修复编译器路径
std::string CompilationDatabaseProcessor::validateCompilerPath(const std::string& original_compiler) const {
    // 如果原编译器路径有效，直接返回
    if (llvm::sys::fs::exists(original_compiler) && llvm::sys::fs::can_execute(original_compiler)) {
        return original_compiler;
    }
    
    std::cout << "⚠️ 无效的编译器路径: " << original_compiler << std::endl;
    
    // 尝试从编译器名称推断正确的路径
    std::string compiler_name = llvm::sys::path::filename(original_compiler).str();
    
    // 常见的编译器查找顺序
    std::vector<std::string> compiler_candidates;
    
    if (compiler_name.find("clang") != std::string::npos) {
        compiler_candidates = {
            "clang++", "clang", 
            "/usr/bin/clang++", "/usr/bin/clang",
            "/usr/local/bin/clang++", "/usr/local/bin/clang"
        };
    } else if (compiler_name.find("gcc") != std::string::npos || compiler_name.find("cc") != std::string::npos) {
        compiler_candidates = {
            "gcc", "g++", "cc",
            "/usr/bin/gcc", "/usr/bin/g++", "/usr/bin/cc",
            "/usr/local/bin/gcc", "/usr/local/bin/g++", "/usr/local/bin/cc"
        };
    } else {
        // 默认编译器顺序
        compiler_candidates = {
            "clang++", "gcc", "g++", "clang", "cc",
            "/usr/bin/clang++", "/usr/bin/gcc", "/usr/bin/g++",
            "/usr/bin/clang", "/usr/bin/cc"
        };
    }
    
    // 测试每个候选编译器
    for (const auto& candidate : compiler_candidates) {
        // 首先检查是否在PATH中
        if (system(("which " + candidate + " > /dev/null 2>&1").c_str()) == 0) {
            std::cout << "✅ 找到替代编译器: " << candidate << std::endl;
            return candidate;
        }
        
        // 检查绝对路径
        if (llvm::sys::fs::exists(candidate) && llvm::sys::fs::can_execute(candidate)) {
            std::cout << "✅ 找到替代编译器: " << candidate << std::endl;
            return candidate;
        }
    }
    
    std::cout << "❌ 找不到有效的编译器" << std::endl;
    return "clang++"; // 返回默认值
}

std::vector<std::string> CompilationDatabaseProcessor::getSourceFiles() const {
    std::vector<std::string> source_files;
    source_files.reserve(compile_commands.size());
    
    for (const auto& cmd : compile_commands) {
        source_files.push_back(cmd.file);
    }
    
    return source_files;
}

std::vector<std::string> CompilationDatabaseProcessor::getCommonCompileOptions() const {
    std::unordered_map<std::string, int> arg_count;

    // 统计所有编译选项的出现次数
    for (const auto& cmd : compile_commands) {
        auto compile_opts = extractCompileOptions(cmd.arguments);
        for (const auto& opt : compile_opts) {
            arg_count[opt]++;
        }
    }

    // 选择通用编译选项（出现在至少30%的编译命令中）
    std::vector<std::string> common_args;
    int threshold = std::max(1, (int)(compile_commands.size() * 0.3));

    for (const auto& [arg, count] : arg_count) {
        if (count >= threshold) {
            common_args.push_back(arg);
        }
    }

    // 如果没有足够的通用选项，添加基本选项
    if (common_args.empty()) {
        common_args = {"-std=c99", "-w"}; // 基本的C99标准和警告抑制
    }

    return common_args;
}

std::vector<std::vector<std::string>> CompilationDatabaseProcessor::createFileBatches(size_t batch_size) const {
    std::vector<std::vector<std::string>> file_batches;
    auto source_files = getSourceFiles();
    
    for (size_t i = 0; i < source_files.size(); i += batch_size) {
        std::vector<std::string> batch;
        for (size_t j = i; j < std::min(i + batch_size, source_files.size()); ++j) {
            batch.push_back(source_files[j]);
        }
        file_batches.push_back(batch);
    }

    return file_batches;
}

//=============================================================================
// 私有辅助方法
//=============================================================================

bool CompilationDatabaseProcessor::isCppSourceFile(const std::string& file_path) const {
    static const std::vector<std::string> cpp_extensions = {
        ".c", ".cc", ".cpp", ".cxx", ".c++", ".C"
    };

    std::string ext = llvm::sys::path::extension(file_path).str();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    return std::find(cpp_extensions.begin(), cpp_extensions.end(), ext) != cpp_extensions.end();
}

std::vector<std::string> CompilationDatabaseProcessor::extractCompileOptions(const std::vector<std::string>& args) const {
    std::vector<std::string> options;
    std::unordered_set<std::string> seen_options;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];

        // 跳过第一个参数（编译器），我们会单独处理
        if (i == 0) {
            continue;
        }

        // 跳过源文件和输出文件
        if (isCppSourceFile(arg) || arg == "-o") {
            if (arg == "-o" && i + 1 < args.size()) {
                ++i;  // 跳过输出文件名
            }
            continue;
        }

        // 只包含有用的编译选项
        if (!shouldIncludeOption(arg)) {
            continue;
        }

        // 对于某些选项，只保留最后一个
        if (shouldDeduplicateOption(arg)) {
            // 移除之前相同类型的选项
            for (auto it = options.begin(); it != options.end();) {
                if ((arg.substr(0, 4) == "-std" && it->substr(0, 4) == "-std") ||
                    (arg.substr(0, 2) == "-O" && it->substr(0, 2) == "-O")) {
                    it = options.erase(it);
                } else {
                    ++it;
                }
            }
        }
        
        // 添加选项，避免重复
        if (seen_options.find(arg) == seen_options.end()) {
            options.push_back(arg);
            seen_options.insert(arg);
        }
    }

    return options;
}

bool CompilationDatabaseProcessor::shouldIncludeOption(const std::string& arg) const {
    // 跳过可能导致冲突的选项
    if (arg == "--no-warn" || arg == "-w" || arg.substr(0, 8) == "-Wno-all" ||
        arg == "--help" || arg == "-h" || arg == "--version" || 
        arg.substr(0, 9) == "--target=" || arg.substr(0, 8) == "-target=" ||
        arg.substr(0, 7) == "-march=" || arg.substr(0, 7) == "-mcpu=" ||
        arg.substr(0, 7) == "-mtune=" ||
        arg == "-v" || arg == "--verbose" || arg == "-pipe" ||
        arg.substr(0, 5) == "-flto" || arg == "-fno-semantic-interposition" ||
        arg.substr(0, 4) == "-Wl," || arg == "-shared" || arg == "-static" ||
        arg.substr(0, 2) == "-l") {
        return false;
    }

    // 包含有用的编译选项
    return (arg.substr(0, 2) == "-I" || arg.substr(0, 2) == "-D" || 
            arg.substr(0, 2) == "-f" || arg.substr(0, 2) == "-W" || 
            arg.substr(0, 4) == "-std" || arg == "-g" ||
            arg.substr(0, 2) == "-m" || arg.substr(0, 2) == "-O");
}

bool CompilationDatabaseProcessor::shouldDeduplicateOption(const std::string& arg) const {
    return (arg.substr(0, 4) == "-std" || arg.substr(0, 2) == "-O");
}

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

        // 转换为绝对路径
        if (!llvm::sys::path::is_absolute(cmd.file)) {
            llvm::SmallString<256> absolute_path(cmd.directory);
            llvm::sys::path::append(absolute_path, cmd.file);
            cmd.file = absolute_path.str().str();
        }

        // 尝试规范化路径
        llvm::SmallString<256> canonical_path;
        if (llvm::sys::fs::real_path(cmd.file, canonical_path)) {
            continue; // 如果无法获取真实路径，跳过这个文件
        }
        cmd.file = canonical_path.str().str();

        if (!isCppSourceFile(cmd.file)) continue;

        // 读取编译参数
        if (entry.isMember("arguments") && entry["arguments"].isArray()) {
            for (const auto& arg : entry["arguments"]) {
                if (arg.isString()) {
                    cmd.arguments.push_back(arg.asString());
                }
            }
        } else if (entry.isMember("command") && entry["command"].isString()) {
            std::string command = entry["command"].asString();
            std::istringstream iss(command);
            std::string arg;
            while (iss >> std::quoted(arg)) {
                cmd.arguments.push_back(arg);
            }
        } else {
            continue;
        }

        compile_commands.push_back(std::move(cmd));
    }

    std::cout << "✅ 加载了 " << compile_commands.size() << " 个源文件的编译信息" << std::endl;
    return !compile_commands.empty();
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

    // 选择通用编译选项（出现在至少50%的编译命令中）
    std::vector<std::string> common_args;
    int threshold = std::max(1, (int)(compile_commands.size() * 0.5));

    for (const auto& [arg, count] : arg_count) {
        if (count >= threshold) {
            common_args.push_back(arg);
        }
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
    std::unordered_set<std::string> seen_options;  // 用于去重

    for (size_t i = 1; i < args.size(); ++i) {  // 跳过编译器名称
        const std::string& arg = args[i];

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
        
        // 添加选项，如果之前没有见过
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
        arg == "-v" || arg == "--verbose") {
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

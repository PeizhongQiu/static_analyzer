#ifndef COMPILATION_DATABASE_H
#define COMPILATION_DATABASE_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

/**
 * 编译命令条目
 */
struct LocalCompileCommand {
    std::string directory;
    std::string file;
    std::vector<std::string> arguments;
};

/**
 * 编译数据库处理类
 */
class CompilationDatabaseProcessor {
private:
    std::string db_path;
    std::vector<LocalCompileCommand> compile_commands;

public:
    explicit CompilationDatabaseProcessor(const std::string& database_path);

    // 加载和处理
    bool loadCompileCommands();
    std::vector<std::string> getSourceFiles() const;
    std::vector<std::string> getCommonCompileOptions() const;
    
    // 批处理支持
    std::vector<std::vector<std::string>> createFileBatches(size_t batch_size = 50) const;

    // 获取原始命令
    const std::vector<LocalCompileCommand>& getCommands() const { return compile_commands; }

private:
    // 辅助方法
    bool isCppSourceFile(const std::string& file_path) const;
    std::vector<std::string> extractCompileOptions(const std::vector<std::string>& args) const;
    bool shouldIncludeOption(const std::string& arg) const;
    bool shouldDeduplicateOption(const std::string& arg) const;
};

#endif // COMPILATION_DATABASE_H

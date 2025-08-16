#ifndef DATA_STRUCTURES_H
#define DATA_STRUCTURES_H
#include <string>
#include <vector>
/* 全局变量写操作信息
 */
struct WriteOperation {
    std::string target;      // 目标变量名
    std::string node_id;     // AST节点ID
    std::string file;        // 源文件路径
    unsigned line;           // 行号
    unsigned column;         // 列号
    std::string function;    // 所在函数
    std::string ast_kind;    // AST节点类型
    std::string write_type;  // variable, data_structure, register
};
/* 函数调用信息
 */
struct CallInfo {
    std::string caller;      // 调用者函数
    std::string callee;      // 被调用函数
    std::string node_id;     // AST节点ID
    std::string file;        // 源文件路径
    unsigned line;           // 行号
    unsigned column;         // 列号
    bool is_indirect;        // 是否是间接调用（函数指针）
};
/* 函数指针调用信息
 */
struct FunctionPointerCall {
    std::string caller;           // 调用者函数
    std::string pointer_name;     // 函数指针变量名
    std::string pointer_type;     // 函数指针类型签名
    std::string node_id;          // AST节点ID
    std::string file;             // 源文件路径
    unsigned line;                // 行号
    unsigned column;              // 列号
    std::vector<std::string> possible_targets;  // 可能的目标函数
};
/* 函数指针赋值信息
 */
struct FunctionPointerAssignment {
    std::string pointer_name;     // 函数指针名称
    std::string target_function;  // 目标函数名
    std::string assignment_type;  // 赋值类型：direct, struct_init, designated_init
    std::string file;             // 源文件路径
    unsigned line;                // 行号
    unsigned column;              // 列号
};
/* 寄存器操作信息
 */
struct RegisterOperation {
    std::string target;      // 寄存器名称或指令类型
    std::string operation;   // read, write, execute
    std::string value;       // 操作值（如果有）
    std::string node_id;     // AST节点ID
    std::string file;        // 源文件路径
    unsigned line;           // 行号
    unsigned column;         // 列号
    std::string function;    // 所在函数
    std::string details;     // 完整的汇编指令
};
/* 函数签名信息
 */
struct FunctionInfo {
    std::string name;
    std::string file;
    unsigned line;
    std::string return_type;
    std::vector<std::string> parameters;
    bool is_static;
    std::string linkage;
};
#endif // DATA_STRUCTURES_H

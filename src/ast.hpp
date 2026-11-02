#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace stackexpr {

struct Loc {
    std::size_t line = 0;
    std::size_t column = 0;
};

// Every compile error carries a source location so the driver can print file:line:col.
struct CompileError : std::runtime_error {
    Loc loc;
    CompileError(Loc l, const std::string& msg) : std::runtime_error(msg), loc(l) {}
};

enum class OpKind {
    PushInt,    // value
    PushConst,  // name: pushes the address of a const
    Add, Sub, Mul, Div, Inc,
    Eq, Ne, Lt, Gt, Le, Ge,
    Dup, Swap, Drop,
    Call,       // name: a stackexpr or an assembly library macro
    Asm,        // text: raw assembly from `invoke "..."`
    If,         // body
    While,      // cond, body
};

struct Op {
    OpKind kind = OpKind::PushInt;
    Loc loc;
    std::int64_t value = 0;
    std::string text;
    std::vector<Op> cond;
    std::vector<Op> body;
};

struct StackExprDef {
    std::string name;
    Loc loc;
    std::vector<Op> body;
};

struct ConstDef {
    std::string name;
    Loc loc;
    bool is_string = false;
    std::string string_value;  // without quotes, escapes left as written
    std::int64_t int_value = 0;
};

struct Include {
    std::string lib;
    Loc loc;
};

struct Program {
    std::vector<Include> includes;
    std::vector<ConstDef> consts;
    std::map<std::string, StackExprDef> stackexprs;
};

}  // namespace stackexpr

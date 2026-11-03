#pragma once

#include "types.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace stackexpr {

struct Loc {
    std::uint32_t file = 0;  // index into the list of loaded files
    std::size_t line = 0;
    std::size_t column = 0;
};

// Every compile error carries a source location so the driver can print file:line:col.
struct CompileError : std::runtime_error {
    Loc loc;
    CompileError(Loc l, const std::string& msg) : std::runtime_error(msg), loc(l) {}
};

enum class OpKind {
    // Values
    PushInt,     // value
    PushBool,    // value (0 or 1)
    PushString,  // text: pushes the address of a 0-terminated copy
    Name,        // text: a local, const, global, or struct field (resolved by the type checker)
    AddrOf,      // text: &name, the address of a stackexpr or interrupt handler
    SizeOf,      // text: sizeof Struct
    // Arithmetic and logic
    Add, Sub, Mul, Div, Mod, UDiv, UMod, Inc,
    Eq, Ne, Lt, Gt, Le, Ge, ULt, UGt, ULe, UGe,
    And, Or, Xor, Not, Shl, Shr, Sar,
    // Memory
    Load8, Load16, Load32, Load64,
    Store8, Store16, Store32, Store64,
    // Stack
    Dup, Drop, Swap, Over, Rot, Nip, Tuck,
    // Hardware
    InB, InW, InD, OutB, OutW, OutD,
    Cli, Sti, Hlt, Lgdt, Lidt, Invlpg, RdMsr, WrMsr,
    RdCr0, WrCr0, RdCr2, RdCr3, WrCr3, RdCr4, WrCr4,
    // Structure
    Call,      // text
    Execute,   // sig: call through an address on the stack
    Asm,       // text, sig: raw assembly from `invoke "..."`
    As,        // type
    If,        // body, else_body
    While,     // cond, body
    Break, Continue, Return,
    Let,       // names, body
    Set,       // text
};

// What a Name op refers to, filled in by the type checker.
enum class NameRef { Unresolved, Local, Const, Global, Field };

struct Op {
    OpKind kind = OpKind::PushInt;
    Loc loc;
    std::int64_t value = 0;
    std::string text;
    std::vector<std::string> names;
    Signature sig;
    Type type;
    std::vector<Op> cond;
    std::vector<Op> body;
    std::vector<Op> else_body;
    bool has_else = false;

    // Filled in by the type checker for the code generator.
    int overload = 0;                 // which variant of an overloaded word applies
    NameRef ref = NameRef::Unresolved;
    std::vector<std::int64_t> slots;  // Let: local slot per name; Name and Set use `value`
};

enum class ExprKind {
    Function,   // stackexpr: a subroutine
    Inline,     // inline stackexpr: pasted at each call site
    Interrupt,  // interrupt: a handler that saves all registers and returns with iretq
};

struct StackExprDef {
    std::string name;
    Loc loc;
    ExprKind kind = ExprKind::Function;
    Signature sig;
    std::vector<Op> body;
    std::int64_t frame_slots = 0;  // number of locals, filled in by the type checker
};

// An assembly macro from a library, with the stack effect it promises.
struct ExternDef {
    std::string name;
    Loc loc;
    Signature sig;
};

// A compile-time value.
struct ConstDef {
    std::string name;
    Loc loc;
    std::int64_t value = 0;
    Type type;
};

enum class GlobalKind {
    Var,      // an 8-byte cell with an initial value
    Data,     // the bytes of a string, followed by 0
    Reserve,  // a zeroed buffer
};

struct GlobalDef {
    std::string name;
    Loc loc;
    GlobalKind kind = GlobalKind::Var;
    std::int64_t init = 0;   // Var
    std::string bytes;       // Data, as written in the source (escapes not yet decoded)
    std::int64_t size = 0;   // Reserve
    std::int64_t align = 8;  // Reserve
};

struct Field {
    std::string name;
    std::int64_t offset = 0;
    std::int64_t size = 0;
};

struct StructDef {
    std::string name;
    Loc loc;
    std::vector<Field> fields;
    std::int64_t size = 0;
};

struct AsmLib {
    std::string name;
    Loc loc;
};

struct Program {
    std::vector<AsmLib> asm_libs;
    std::map<std::string, StackExprDef> stackexprs;
    std::map<std::string, ExternDef> externs;
    std::map<std::string, ConstDef> consts;
    std::map<std::string, GlobalDef> globals;
    std::map<std::string, StructDef> structs;
    std::map<std::string, Loc> names;  // every top-level name, to detect duplicates
};

}  // namespace stackexpr

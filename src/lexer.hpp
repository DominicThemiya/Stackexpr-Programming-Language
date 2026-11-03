#pragma once

#include "lexengine.hpp"

#include <string_view>
#include <vector>

namespace stackexpr {

enum class Tok {
    // Keywords
    Grab, StackExpr, Inline, Interrupt, Extern, End, Call,
    If, Else, While, Do, Break, Continue, Return,
    Const, Var, Data, Reserve, Struct, Sizeof,
    Let, In, Set, As, Execute, Invoke, True, False,
    // Literals and names. Built-in words such as `dup` and `outb` are identifiers;
    // the parser recognises them by name.
    Ident, Int, Float, String,
    // Operator symbols: + - * / ++ == != < > <= >= @8..@64 !8..!64
    Op,
    // Punctuation
    LParen, RParen, DashDash, Colon, Amp,
    // Control
    Eof, Error,
};

using Token = lexengine::Token<Tok>;

// The language definition, built once.
const lexengine::LangDef<Tok>& language();

// Lexes `source`. The returned tokens point into `source`, which must outlive them.
std::vector<Token> lex(std::string_view source);

const char* describe(Tok t);

}  // namespace stackexpr

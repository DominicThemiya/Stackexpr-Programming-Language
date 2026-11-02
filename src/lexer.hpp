#pragma once

#include "lexengine.hpp"

#include <string_view>
#include <vector>

namespace stackexpr {

enum class Tok {
    // Keywords
    Grab, StackExpr, End, Call, If, While, Do, Const, Invoke,
    Dup, Swap, Drop,
    Mod, UDiv, UMod, ULt, UGt, ULe, UGe,
    And, Or, Xor, Not, Shl, Shr, Sar,
    // Literals and names
    Ident, Int, Float, String,
    // Operators
    Plus, PlusPlus, Minus, Star, Slash,
    Eq, Ne, Lt, Gt, Le, Ge,
    Load8, Load16, Load32, Load64,
    Store8, Store16, Store32, Store64,
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

#include "lexer.hpp"

namespace stackexpr {

static lexengine::LangDef<Tok> build_language() {
    lexengine::LangDef<Tok> lang(Tok::Eof, Tok::Error);
    lang.keyword("grab", Tok::Grab)
        .keyword("stackexpr", Tok::StackExpr)
        .keyword("end", Tok::End)
        .keyword("call", Tok::Call)
        .keyword("if", Tok::If)
        .keyword("while", Tok::While)
        .keyword("do", Tok::Do)
        .keyword("const", Tok::Const)
        .keyword("invoke", Tok::Invoke)
        .keyword("dup", Tok::Dup)
        .keyword("swap", Tok::Swap)
        .keyword("drop", Tok::Drop)
        .keyword("mod", Tok::Mod)
        .keyword("udiv", Tok::UDiv)
        .keyword("umod", Tok::UMod)
        .keyword("ult", Tok::ULt)
        .keyword("ugt", Tok::UGt)
        .keyword("ule", Tok::ULe)
        .keyword("uge", Tok::UGe)
        .keyword("and", Tok::And)
        .keyword("or", Tok::Or)
        .keyword("xor", Tok::Xor)
        .keyword("not", Tok::Not)
        .keyword("shl", Tok::Shl)
        .keyword("shr", Tok::Shr)
        .keyword("sar", Tok::Sar)
        .symbol("+", Tok::Plus)
        .symbol("++", Tok::PlusPlus)
        .symbol("-", Tok::Minus)
        .symbol("*", Tok::Star)
        .symbol("/", Tok::Slash)
        .symbol("==", Tok::Eq)
        .symbol("!=", Tok::Ne)
        .symbol("<", Tok::Lt)
        .symbol(">", Tok::Gt)
        .symbol("<=", Tok::Le)
        .symbol(">=", Tok::Ge)
        .symbol("@8", Tok::Load8)
        .symbol("@16", Tok::Load16)
        .symbol("@32", Tok::Load32)
        .symbol("@64", Tok::Load64)
        .symbol("!8", Tok::Store8)
        .symbol("!16", Tok::Store16)
        .symbol("!32", Tok::Store32)
        .symbol("!64", Tok::Store64)
        .identifier(Tok::Ident)
        .string_literal(Tok::String, "\"", "\"")
        .string_literal(Tok::String, "'", "'")
        .line_comment("//");

    lexengine::LangDef<Tok>::NumberRule numbers{Tok::Int, Tok::Float};
    numbers.allow_binary = true;
    numbers.digit_separator = '_';
    lang.numbers(numbers);
    return lang;
}

const lexengine::LangDef<Tok>& language() {
    static const lexengine::LangDef<Tok> lang = build_language();
    return lang;
}

std::vector<Token> lex(std::string_view source) {
    return lexengine::tokenize(language(), source);
}

const char* describe(Tok t) {
    switch (t) {
        case Tok::Grab: return "'grab'";
        case Tok::StackExpr: return "'stackexpr'";
        case Tok::End: return "'end'";
        case Tok::Call: return "'call'";
        case Tok::If: return "'if'";
        case Tok::While: return "'while'";
        case Tok::Do: return "'do'";
        case Tok::Const: return "'const'";
        case Tok::Invoke: return "'invoke'";
        case Tok::Dup: return "'dup'";
        case Tok::Swap: return "'swap'";
        case Tok::Drop: return "'drop'";
        case Tok::Mod: return "'mod'";
        case Tok::UDiv: return "'udiv'";
        case Tok::UMod: return "'umod'";
        case Tok::ULt: return "'ult'";
        case Tok::UGt: return "'ugt'";
        case Tok::ULe: return "'ule'";
        case Tok::UGe: return "'uge'";
        case Tok::And: return "'and'";
        case Tok::Or: return "'or'";
        case Tok::Xor: return "'xor'";
        case Tok::Not: return "'not'";
        case Tok::Shl: return "'shl'";
        case Tok::Shr: return "'shr'";
        case Tok::Sar: return "'sar'";
        case Tok::Load8: return "'@8'";
        case Tok::Load16: return "'@16'";
        case Tok::Load32: return "'@32'";
        case Tok::Load64: return "'@64'";
        case Tok::Store8: return "'!8'";
        case Tok::Store16: return "'!16'";
        case Tok::Store32: return "'!32'";
        case Tok::Store64: return "'!64'";
        case Tok::Ident: return "identifier";
        case Tok::Int: return "integer";
        case Tok::Float: return "float";
        case Tok::String: return "string";
        case Tok::Plus: return "'+'";
        case Tok::PlusPlus: return "'++'";
        case Tok::Minus: return "'-'";
        case Tok::Star: return "'*'";
        case Tok::Slash: return "'/'";
        case Tok::Eq: return "'=='";
        case Tok::Ne: return "'!='";
        case Tok::Lt: return "'<'";
        case Tok::Gt: return "'>'";
        case Tok::Le: return "'<='";
        case Tok::Ge: return "'>='";
        case Tok::Eof: return "end of file";
        case Tok::Error: return "invalid input";
    }
    return "?";
}

}  // namespace stackexpr

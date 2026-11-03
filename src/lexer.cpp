#include "lexer.hpp"

#include <cctype>

namespace stackexpr {

static lexengine::LangDef<Tok> build_language() {
    lexengine::LangDef<Tok> lang(Tok::Eof, Tok::Error);
    lang.keyword("grab", Tok::Grab)
        .keyword("stackexpr", Tok::StackExpr)
        .keyword("inline", Tok::Inline)
        .keyword("interrupt", Tok::Interrupt)
        .keyword("extern", Tok::Extern)
        .keyword("end", Tok::End)
        .keyword("call", Tok::Call)
        .keyword("if", Tok::If)
        .keyword("else", Tok::Else)
        .keyword("while", Tok::While)
        .keyword("do", Tok::Do)
        .keyword("break", Tok::Break)
        .keyword("continue", Tok::Continue)
        .keyword("return", Tok::Return)
        .keyword("const", Tok::Const)
        .keyword("var", Tok::Var)
        .keyword("data", Tok::Data)
        .keyword("reserve", Tok::Reserve)
        .keyword("struct", Tok::Struct)
        .keyword("sizeof", Tok::Sizeof)
        .keyword("let", Tok::Let)
        .keyword("in", Tok::In)
        .keyword("set", Tok::Set)
        .keyword("execute", Tok::Execute)
        .keyword("invoke", Tok::Invoke)
        .keyword("true", Tok::True)
        .keyword("false", Tok::False);

    for (const char* op : {"+", "++", "-", "*", "/", "==", "!=", "<", ">", "<=", ">=",
                           "@8", "@16", "@32", "@64", "!8", "!16", "!32", "!64"}) {
        lang.symbol(op, Tok::Op);
    }
    lang.symbol("(", Tok::LParen)
        .symbol(")", Tok::RParen)
        .symbol("--", Tok::DashDash)
        .symbol(":", Tok::Colon)
        .symbol("&", Tok::Amp)
        .symbol("~", Tok::Tilde);

    // Identifiers may contain '.', so struct fields read as `Point.x`.
    lexengine::LangDef<Tok>::IdentifierRule ident{Tok::Ident};
    ident.is_start = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; };
    ident.is_continue = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.'; };
    lang.identifier(ident);

    lang.string_literal(Tok::String, "\"", "\"")
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
        case Tok::Inline: return "'inline'";
        case Tok::Interrupt: return "'interrupt'";
        case Tok::Extern: return "'extern'";
        case Tok::End: return "'end'";
        case Tok::Call: return "'call'";
        case Tok::If: return "'if'";
        case Tok::Else: return "'else'";
        case Tok::While: return "'while'";
        case Tok::Do: return "'do'";
        case Tok::Break: return "'break'";
        case Tok::Continue: return "'continue'";
        case Tok::Return: return "'return'";
        case Tok::Const: return "'const'";
        case Tok::Var: return "'var'";
        case Tok::Data: return "'data'";
        case Tok::Reserve: return "'reserve'";
        case Tok::Struct: return "'struct'";
        case Tok::Sizeof: return "'sizeof'";
        case Tok::Let: return "'let'";
        case Tok::In: return "'in'";
        case Tok::Set: return "'set'";
        case Tok::Execute: return "'execute'";
        case Tok::Invoke: return "'invoke'";
        case Tok::True: return "'true'";
        case Tok::False: return "'false'";
        case Tok::Ident: return "identifier";
        case Tok::Int: return "integer";
        case Tok::Float: return "float";
        case Tok::String: return "string";
        case Tok::Op: return "operator";
        case Tok::LParen: return "'('";
        case Tok::RParen: return "')'";
        case Tok::DashDash: return "'--'";
        case Tok::Colon: return "':'";
        case Tok::Amp: return "'&'";
        case Tok::Tilde: return "'~'";
        case Tok::Eof: return "end of file";
        case Tok::Error: return "invalid input";
    }
    return "?";
}

}  // namespace stackexpr

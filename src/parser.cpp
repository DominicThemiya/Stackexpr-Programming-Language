#include "parser.hpp"

#include <charconv>
#include <string>

namespace stackexpr {

namespace {

Loc loc_of(const Token& t) { return {t.line, t.column}; }

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens) : toks_(tokens) {}

    Program run() {
        Program prog;
        while (peek().type != Tok::Eof) {
            const Token& t = peek();
            switch (t.type) {
                case Tok::Grab: prog.includes.push_back(parse_grab()); break;
                case Tok::Const: prog.consts.push_back(parse_const()); break;
                case Tok::StackExpr: {
                    StackExprDef def = parse_stackexpr();
                    auto existing = prog.stackexprs.find(def.name);
                    if (existing != prog.stackexprs.end()) {
                        throw CompileError(def.loc, "stackexpr '" + def.name + "' is already defined at line " +
                                                        std::to_string(existing->second.loc.line));
                    }
                    std::string name = def.name;
                    prog.stackexprs.emplace(std::move(name), std::move(def));
                    break;
                }
                case Tok::Ident:
                    if (t.text == "entry") {
                        throw CompileError(loc_of(t),
                                           "'entry' is no longer supported; define the entry point with "
                                           "'stackexpr main ... end'");
                    }
                    [[fallthrough]];
                default:
                    throw CompileError(loc_of(t), std::string("unexpected ") + describe(t.type) +
                                                      " at top level; code must be inside a stackexpr "
                                                      "(the program starts at 'stackexpr main')");
            }
        }

        if (prog.stackexprs.find("main") == prog.stackexprs.end()) {
            throw CompileError(loc_of(peek()),
                               "no entry point; define one with 'stackexpr main ... end'");
        }
        return prog;
    }

private:
    const std::vector<Token>& toks_;
    std::size_t pos_ = 0;

    const Token& peek(std::size_t ahead = 0) const {
        std::size_t i = pos_ + ahead;
        return i < toks_.size() ? toks_[i] : toks_.back();  // the stream always ends with Eof
    }

    const Token& advance() {
        const Token& t = peek();
        if (t.type == Tok::Error) {
            throw CompileError(loc_of(t), "invalid input '" + std::string(t.text) + "'");
        }
        if (pos_ < toks_.size() - 1) ++pos_;
        return t;
    }

    const Token& expect(Tok type, const char* context) {
        const Token& t = peek();
        if (t.type != type) {
            throw CompileError(loc_of(t), std::string("expected ") + describe(type) + " " + context +
                                              ", found " + describe(t.type));
        }
        return advance();
    }

    Include parse_grab() {
        const Token& grab = advance();
        // grab asm<name>
        const Token& kind = expect(Tok::Ident, "after 'grab'");
        if (kind.text != "asm") {
            throw CompileError(loc_of(kind), "unsupported library kind '" + std::string(kind.text) +
                                                 "'; only asm<...> libraries are supported");
        }
        expect(Tok::Lt, "after 'asm'");
        const Token& name = expect(Tok::Ident, "as the library name");
        expect(Tok::Gt, "to close the library name");
        return {std::string(name.text), loc_of(grab)};
    }

    ConstDef parse_const() {
        advance();  // const
        const Token& name = expect(Tok::Ident, "after 'const'");
        ConstDef def;
        def.name = std::string(name.text);
        def.loc = loc_of(name);

        const Token& v = peek();
        if (v.type == Tok::String) {
            advance();
            def.is_string = true;
            def.string_value = std::string(v.text.substr(1, v.text.size() - 2));
        } else if (v.type == Tok::Int || v.type == Tok::Minus) {
            def.int_value = parse_int_literal();
        } else {
            throw CompileError(loc_of(v), std::string("expected a string or integer value for const '") +
                                              def.name + "', found " + describe(v.type));
        }
        return def;
    }

    StackExprDef parse_stackexpr() {
        advance();  // stackexpr
        const Token& name = expect(Tok::Ident, "after 'stackexpr'");
        StackExprDef def;
        def.name = std::string(name.text);
        def.loc = loc_of(name);
        def.body = parse_block({Tok::End}, "stackexpr '" + def.name + "'");
        advance();  // end
        return def;
    }

    // Parses ops until one of `terminators` is next (not consumed).
    std::vector<Op> parse_block(std::initializer_list<Tok> terminators, const std::string& what) {
        std::vector<Op> ops;
        for (;;) {
            const Token& t = peek();
            for (Tok term : terminators) {
                if (t.type == term) return ops;
            }
            if (t.type == Tok::Eof) {
                throw CompileError(loc_of(t), "unterminated " + what + "; missing 'end'");
            }
            ops.push_back(parse_op());
        }
    }

    std::int64_t parse_int_literal() {
        bool negative = false;
        if (peek().type == Tok::Minus) {
            const Token& minus = advance();
            const Token& digits = peek();
            if (digits.type != Tok::Int || digits.offset != minus.offset + 1) {
                throw CompileError(loc_of(minus), "expected an integer after '-'");
            }
            negative = true;
        }
        const Token& t = expect(Tok::Int, "");
        std::string text;
        for (char c : t.text) {
            if (c != '_') text += c;
        }
        int base = 10;
        std::size_t start = 0;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) base = 16, start = 2;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) base = 2, start = 2;

        std::uint64_t magnitude = 0;
        auto [end, ec] = std::from_chars(text.data() + start, text.data() + text.size(), magnitude, base);
        const std::uint64_t limit = negative ? (std::uint64_t{1} << 63) : (std::uint64_t{1} << 63) - 1;
        if (ec != std::errc() || end != text.data() + text.size() || magnitude > limit) {
            throw CompileError(loc_of(t), "integer literal '" + std::string(t.text) + "' does not fit in 64 bits");
        }
        return negative ? static_cast<std::int64_t>(0 - magnitude) : static_cast<std::int64_t>(magnitude);
    }

    Op simple(OpKind kind, const Token& t) {
        Op op;
        op.kind = kind;
        op.loc = loc_of(t);
        return op;
    }

    Op parse_op() {
        const Token& t = peek();
        switch (t.type) {
            case Tok::Int: {
                Op op = simple(OpKind::PushInt, t);
                op.value = parse_int_literal();
                return op;
            }
            case Tok::Minus: {
                // `-5` (no space) is a negative literal; a lone `-` is subtraction.
                const Token& next = peek(1);
                if (next.type == Tok::Int && next.offset == t.offset + 1) {
                    Op op = simple(OpKind::PushInt, t);
                    op.value = parse_int_literal();
                    return op;
                }
                advance();
                return simple(OpKind::Sub, t);
            }
            case Tok::Plus: advance(); return simple(OpKind::Add, t);
            case Tok::PlusPlus: advance(); return simple(OpKind::Inc, t);
            case Tok::Star: advance(); return simple(OpKind::Mul, t);
            case Tok::Slash: advance(); return simple(OpKind::Div, t);
            case Tok::Eq: advance(); return simple(OpKind::Eq, t);
            case Tok::Ne: advance(); return simple(OpKind::Ne, t);
            case Tok::Lt: advance(); return simple(OpKind::Lt, t);
            case Tok::Gt: advance(); return simple(OpKind::Gt, t);
            case Tok::Le: advance(); return simple(OpKind::Le, t);
            case Tok::Ge: advance(); return simple(OpKind::Ge, t);
            case Tok::Dup: advance(); return simple(OpKind::Dup, t);
            case Tok::Swap: advance(); return simple(OpKind::Swap, t);
            case Tok::Drop: advance(); return simple(OpKind::Drop, t);

            case Tok::Call: {
                advance();
                const Token& name = expect(Tok::Ident, "after 'call'");
                Op op = simple(OpKind::Call, t);
                op.text = std::string(name.text);
                return op;
            }
            case Tok::Invoke: {
                advance();
                const Token& s = expect(Tok::String, "after 'invoke'");
                Op op = simple(OpKind::Asm, t);
                op.text = std::string(s.text.substr(1, s.text.size() - 2));
                return op;
            }
            case Tok::Ident: {
                advance();
                Op op = simple(OpKind::PushConst, t);
                op.text = std::string(t.text);
                return op;
            }
            case Tok::If: {
                advance();
                Op op = simple(OpKind::If, t);
                op.body = parse_block({Tok::End}, "'if'");
                advance();  // end
                return op;
            }
            case Tok::While: {
                advance();
                Op op = simple(OpKind::While, t);
                op.cond = parse_block({Tok::Do, Tok::End}, "'while'");
                expect(Tok::Do, "after the 'while' condition");
                op.body = parse_block({Tok::End}, "'while'");
                advance();  // end
                return op;
            }

            case Tok::StackExpr:
                throw CompileError(loc_of(t), "stackexprs cannot be nested; missing 'end' before this?");
            case Tok::Grab:
            case Tok::Const:
                throw CompileError(loc_of(t), std::string(describe(t.type)) + " is only allowed at top level");
            case Tok::Do:
                throw CompileError(loc_of(t), "'do' without a matching 'while'");
            case Tok::Float:
                throw CompileError(loc_of(t), "floating point numbers are not supported");
            case Tok::String:
                throw CompileError(loc_of(t), "string literals can only be used with 'const' or 'invoke'");
            case Tok::Error:
                throw CompileError(loc_of(t), "invalid input '" + std::string(t.text) + "'");
            case Tok::End:
            case Tok::Eof:
                break;
        }
        throw CompileError(loc_of(t), std::string("unexpected ") + describe(t.type));
    }
};

}  // namespace

Program parse(const std::vector<Token>& tokens) {
    return Parser(tokens).run();
}

}  // namespace stackexpr

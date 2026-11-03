#include "parser.hpp"

#include "lexer.hpp"

#include <charconv>
#include <fstream>
#include <set>
#include <sstream>

namespace stackexpr {

namespace fs = std::filesystem;

const std::map<std::string, OpKind>& builtin_words() {
    static const std::map<std::string, OpKind> words = {
        {"mod", OpKind::Mod},       {"udiv", OpKind::UDiv},     {"umod", OpKind::UMod},
        {"ult", OpKind::ULt},       {"ugt", OpKind::UGt},       {"ule", OpKind::ULe},
        {"uge", OpKind::UGe},       {"and", OpKind::And},       {"or", OpKind::Or},
        {"xor", OpKind::Xor},       {"not", OpKind::Not},       {"shl", OpKind::Shl},
        {"shr", OpKind::Shr},       {"sar", OpKind::Sar},       {"dup", OpKind::Dup},
        {"drop", OpKind::Drop},     {"swap", OpKind::Swap},     {"over", OpKind::Over},
        {"rot", OpKind::Rot},       {"nip", OpKind::Nip},       {"tuck", OpKind::Tuck},
        {"inb", OpKind::InB},       {"inw", OpKind::InW},       {"ind", OpKind::InD},
        {"outb", OpKind::OutB},     {"outw", OpKind::OutW},     {"outd", OpKind::OutD},
        {"cli", OpKind::Cli},       {"sti", OpKind::Sti},       {"hlt", OpKind::Hlt},
        {"lgdt", OpKind::Lgdt},     {"lidt", OpKind::Lidt},     {"invlpg", OpKind::Invlpg},
        {"rdmsr", OpKind::RdMsr},   {"wrmsr", OpKind::WrMsr},   {"rdcr0", OpKind::RdCr0},
        {"wrcr0", OpKind::WrCr0},   {"rdcr2", OpKind::RdCr2},   {"rdcr3", OpKind::RdCr3},
        {"wrcr3", OpKind::WrCr3},   {"rdcr4", OpKind::RdCr4},   {"wrcr4", OpKind::WrCr4},
    };
    return words;
}

static const std::map<std::string, OpKind>& symbol_ops() {
    static const std::map<std::string, OpKind> ops = {
        {"+", OpKind::Add},      {"-", OpKind::Sub},      {"*", OpKind::Mul},      {"/", OpKind::Div},
        {"++", OpKind::Inc},     {"==", OpKind::Eq},      {"!=", OpKind::Ne},      {"<", OpKind::Lt},
        {">", OpKind::Gt},       {"<=", OpKind::Le},      {">=", OpKind::Ge},      {"@8", OpKind::Load8},
        {"@16", OpKind::Load16}, {"@32", OpKind::Load32}, {"@64", OpKind::Load64}, {"!8", OpKind::Store8},
        {"!16", OpKind::Store16}, {"!32", OpKind::Store32}, {"!64", OpKind::Store64},
    };
    return ops;
}

bool is_reserved_name(const std::string& name) {
    return builtin_words().count(name) || name == "int" || name == "bool" || name == "ptr";
}

namespace {

Loc loc_of(std::uint32_t file, const Token& t) { return {file, t.line, t.column}; }

struct Grabs {
    std::vector<std::pair<std::string, Loc>> files;     // grab "path.sxp"
    std::vector<std::pair<std::string, Loc>> asm_libs;  // grab asm<name>
};

// Parses one source file, adding its definitions to `prog`.
class Parser {
public:
    Parser(const std::vector<Token>& tokens, std::uint32_t file, Program& prog,
           const std::vector<SourceFile>& files)
        : toks_(tokens), file_(file), prog_(prog), files_(files) {}

    Grabs run() {
        Grabs grabs;
        while (peek().type != Tok::Eof) {
            const Token& t = peek();
            switch (t.type) {
                case Tok::Grab: parse_grab(grabs); break;
                case Tok::StackExpr: parse_stackexpr(ExprKind::Function); break;
                case Tok::Interrupt: parse_stackexpr(ExprKind::Interrupt); break;
                case Tok::Inline:
                    advance();
                    if (peek().type != Tok::StackExpr) {
                        throw error(peek(), "expected 'stackexpr' after 'inline'");
                    }
                    parse_stackexpr(ExprKind::Inline);
                    break;
                case Tok::Extern: parse_extern(); break;
                case Tok::Const: parse_const(); break;
                case Tok::Var: parse_var(); break;
                case Tok::Data: parse_data(); break;
                case Tok::Reserve: parse_reserve(); break;
                case Tok::Struct: parse_struct(); break;
                case Tok::Ident:
                    if (t.text == "entry") {
                        throw error(t, "'entry' is no longer supported; define the entry point with "
                                       "'stackexpr main ... end'");
                    }
                    [[fallthrough]];
                default:
                    throw error(t, std::string("unexpected ") + describe(t.type) +
                                       " at top level; code must be inside a stackexpr "
                                       "(the program starts at 'stackexpr main')");
            }
        }
        return grabs;
    }

private:
    const std::vector<Token>& toks_;
    std::uint32_t file_;
    Program& prog_;
    const std::vector<SourceFile>& files_;
    std::size_t pos_ = 0;

    CompileError error(const Token& t, const std::string& msg) const { return CompileError(loc_of(file_, t), msg); }

    const Token& peek(std::size_t ahead = 0) const {
        std::size_t i = pos_ + ahead;
        return i < toks_.size() ? toks_[i] : toks_.back();  // the stream always ends with Eof
    }

    const Token& advance() {
        const Token& t = peek();
        if (t.type == Tok::Error) throw error(t, "invalid input '" + std::string(t.text) + "'");
        if (pos_ < toks_.size() - 1) ++pos_;
        return t;
    }

    const Token& expect(Tok type, const std::string& context) {
        const Token& t = peek();
        if (t.type != type) {
            throw error(t, std::string("expected ") + describe(type) + " " + context + ", found " +
                               describe(t.type) + (t.text.empty() ? "" : " '" + std::string(t.text) + "'"));
        }
        return advance();
    }

    bool peek_op(const char* text) const { return peek().type == Tok::Op && peek().text == text; }

    // Registers a top-level name, rejecting duplicates and reserved words.
    void define(const std::string& name, Loc loc) {
        if (is_reserved_name(name)) {
            throw CompileError(loc, "'" + name + "' is a built-in word and can't be used as a name");
        }
        auto [it, inserted] = prog_.names.emplace(name, loc);
        if (!inserted) {
            throw CompileError(loc, "'" + name + "' is already defined at " + files_[it->second.file].path + ":" +
                                        std::to_string(it->second.line));
        }
    }

    // ---- top level --------------------------------------------------------

    void parse_grab(Grabs& grabs) {
        const Token& grab = advance();
        if (peek().type == Tok::String) {
            const Token& path = advance();
            grabs.files.emplace_back(std::string(path.text.substr(1, path.text.size() - 2)), loc_of(file_, grab));
            return;
        }
        const Token& kind = expect(Tok::Ident, "after 'grab' (a \"file.sxp\" path or asm<name>)");
        if (kind.text != "asm") {
            throw error(kind, "unsupported library kind '" + std::string(kind.text) +
                                  "'; use grab \"file.sxp\" or grab asm<name>");
        }
        if (!peek_op("<")) throw error(peek(), "expected '<' after 'asm'");
        advance();
        const Token& name = expect(Tok::Ident, "as the library name");
        if (!peek_op(">")) throw error(peek(), "expected '>' to close the library name");
        advance();
        grabs.asm_libs.emplace_back(std::string(name.text), loc_of(file_, grab));
    }

    void parse_stackexpr(ExprKind kind) {
        advance();  // stackexpr / interrupt
        const Token& name = expect(Tok::Ident, kind == ExprKind::Interrupt ? "after 'interrupt'" : "after 'stackexpr'");
        StackExprDef def;
        def.name = std::string(name.text);
        def.loc = loc_of(file_, name);
        def.kind = kind;
        define(def.name, def.loc);

        if (peek().type == Tok::LParen) {
            def.sig = parse_signature();
        } else if (kind != ExprKind::Interrupt && def.name != "main") {
            throw error(peek(), "stackexpr '" + def.name +
                                    "' needs a stack effect after its name, such as (int -- int) or ( -- )");
        }
        def.body = parse_block({Tok::End}, "stackexpr '" + def.name + "'");
        advance();  // end
        std::string key = def.name;
        prog_.stackexprs.emplace(std::move(key), std::move(def));
    }

    void parse_extern() {
        advance();  // extern
        const Token& name = expect(Tok::Ident, "after 'extern'");
        ExternDef def;
        def.name = std::string(name.text);
        def.loc = loc_of(file_, name);
        define(def.name, def.loc);
        if (peek().type != Tok::LParen) {
            throw error(peek(), "extern '" + def.name + "' needs a stack effect, such as (int --)");
        }
        def.sig = parse_signature();
        std::string key = def.name;
        prog_.externs.emplace(std::move(key), std::move(def));
    }

    void parse_const() {
        advance();  // const
        const Token& name = expect(Tok::Ident, "after 'const'");
        ConstDef def;
        def.name = std::string(name.text);
        def.loc = loc_of(file_, name);
        define(def.name, def.loc);
        if (peek().type == Tok::True || peek().type == Tok::False) {
            def.value = advance().type == Tok::True ? 1 : 0;
            def.type = Type::bool_();
        } else {
            def.value = parse_int_literal("for const '" + def.name + "'");
            def.type = Type::int_();
        }
        if (peek().type == Tok::Tilde) {
            def.type = parse_conversion(advance());
        } else if (peek().type == Tok::Ident && peek().text == "as") {
            throw old_as_error(peek());
        }
        std::string key = def.name;
        prog_.consts.emplace(std::move(key), std::move(def));
    }

    GlobalDef start_global(GlobalKind kind, const char* keyword) {
        advance();  // the keyword
        const Token& name = expect(Tok::Ident, std::string("after '") + keyword + "'");
        GlobalDef def;
        def.name = std::string(name.text);
        def.loc = loc_of(file_, name);
        def.kind = kind;
        define(def.name, def.loc);
        return def;
    }

    void add_global(GlobalDef def) {
        std::string key = def.name;
        prog_.globals.emplace(std::move(key), std::move(def));
    }

    void parse_var() {
        GlobalDef def = start_global(GlobalKind::Var, "var");
        if (peek().type == Tok::Int || peek_op("-")) def.init = parse_int_literal("as the initial value");
        add_global(std::move(def));
    }

    void parse_data() {
        GlobalDef def = start_global(GlobalKind::Data, "data");
        const Token& s = expect(Tok::String, "after the data name");
        def.bytes = std::string(s.text.substr(1, s.text.size() - 2));
        add_global(std::move(def));
    }

    // reserve NAME COUNT [TYPE] [align N]; COUNT is a number or a const name.
    void parse_reserve() {
        GlobalDef def = start_global(GlobalKind::Reserve, "reserve");
        const Token& count_tok = peek();
        def.count_loc = loc_of(file_, count_tok);
        if (count_tok.type == Tok::Ident) {
            def.count_name = std::string(advance().text);
        } else {
            def.count = parse_int_literal("as the size in bytes, or the number of elements");
            if (def.count <= 0) throw error(count_tok, "reserve size must be positive");
        }
        if (peek().type == Tok::Ident && peek().text != "align") {
            const Token& type = advance();
            def.elem_type = std::string(type.text);
            def.elem_loc = loc_of(file_, type);
        }
        def.align = 8;
        if (peek().type == Tok::Ident && peek().text == "align") {
            advance();
            const Token& align_tok = peek();
            def.align = parse_int_literal("after 'align'");
            if (def.align <= 0 || (def.align & (def.align - 1)) != 0) {
                throw error(align_tok, "alignment must be a power of two");
            }
        }
        add_global(std::move(def));
    }

    void parse_struct() {
        advance();  // struct
        const Token& name = expect(Tok::Ident, "after 'struct'");
        StructDef def;
        def.name = std::string(name.text);
        def.loc = loc_of(file_, name);
        define(def.name, def.loc);
        static const std::map<std::string, std::int64_t> sizes = {
            {"u8", 1}, {"u16", 2}, {"u32", 4}, {"u64", 8}, {"int", 8}, {"ptr", 8}, {"bool", 8},
        };
        while (peek().type != Tok::End) {
            const Token& field = expect(Tok::Ident, "as a field name (or 'end' to finish the struct)");
            const Token& type = expect(Tok::Ident, "as the field's type (u8, u16, u32, u64, int, ptr or bool)");
            auto size = sizes.find(std::string(type.text));
            if (size == sizes.end()) {
                throw error(type, "unknown field type '" + std::string(type.text) +
                                      "'; use u8, u16, u32, u64, int, ptr or bool");
            }
            for (const Field& f : def.fields) {
                if (f.name == field.text) throw error(field, "duplicate field '" + f.name + "'");
            }
            def.fields.push_back({std::string(field.text), def.size, size->second});
            def.size += size->second;
        }
        advance();  // end
        std::string key = def.name;
        prog_.structs.emplace(std::move(key), std::move(def));
    }

    // ---- types ------------------------------------------------------------

    Type parse_type_name(const Token& t, bool allow_var) {
        std::string name(t.text);
        if (name == "int") return Type::int_();
        if (name == "bool") return Type::bool_();
        if (name == "ptr") return Type::ptr();
        if (allow_var && name.size() == 1 && name[0] >= 'a' && name[0] <= 'z') return Type::variable(name);
        throw error(t, "unknown type '" + name + "'; the types are int, bool and ptr" +
                           (allow_var ? ", and a single letter such as 'a' stands for any type" : ""));
    }

    // `~ptr`: the type name must follow the `~` directly.
    Type parse_conversion(const Token& tilde) {
        const Token& name = peek();
        if (name.type != Tok::Ident || name.offset != tilde.offset + 1) {
            throw error(tilde, "expected a type right after '~': ~int, ~bool or ~ptr");
        }
        advance();
        return parse_type_name(name, false);
    }

    // Conversions used to be written `as ptr`.
    CompileError old_as_error(const Token& as) const {
        const Token& next = peek(1);
        std::string type = next.type == Tok::Ident ? std::string(next.text) : "ptr";
        return error(as, "conversions are written ~" + type + " (no space), not 'as " + type + "'");
    }

    // ( [name:] type ... -- [name:] type ... )
    Signature parse_signature() {
        expect(Tok::LParen, "to start a stack effect");
        Signature sig;
        bool outputs = false;
        for (;;) {
            const Token& t = peek();
            if (t.type == Tok::DashDash) {
                if (outputs) throw error(t, "a stack effect has only one '--'");
                outputs = true;
                advance();
            } else if (t.type == Tok::RParen) {
                if (!outputs) throw error(t, "missing '--' in stack effect; write (inputs -- outputs)");
                advance();
                return sig;
            } else if (t.type == Tok::Ident) {
                advance();
                const Token* type_tok = &t;
                if (peek().type == Tok::Colon) {  // a documentation name, `x: int`
                    advance();
                    type_tok = &expect(Tok::Ident, "as the type after ':'");
                }
                (outputs ? sig.out : sig.in).push_back(parse_type_name(*type_tok, true));
            } else {
                throw error(t, std::string("expected a type, '--' or ')' in stack effect, found ") + describe(t.type));
            }
        }
    }

    // ---- bodies -----------------------------------------------------------

    // Parses ops until one of `terminators` is next (not consumed).
    std::vector<Op> parse_block(std::initializer_list<Tok> terminators, const std::string& what) {
        std::vector<Op> ops;
        for (;;) {
            const Token& t = peek();
            for (Tok term : terminators) {
                if (t.type == term) return ops;
            }
            if (t.type == Tok::Eof) throw error(t, "unterminated " + what + "; missing 'end'");
            ops.push_back(parse_op());
        }
    }

    // 'A' is 65. Supports the escapes \n \t \r \0 \e \\ \' \" and \xHH.
    std::int64_t parse_char_literal() {
        const Token& t = advance();
        std::string_view body = t.text.substr(1, t.text.size() - 2);
        auto bad = [&](const std::string& why) {
            return error(t, why + "; a character literal holds one byte, such as 'A' or '\\n' (use \"...\" for strings)");
        };
        if (body.empty()) throw bad("empty character literal");
        if (body[0] != '\\') {
            if (body.size() != 1) throw bad("more than one character in " + std::string(t.text));
            return static_cast<unsigned char>(body[0]);
        }
        if (body.size() == 2) {
            switch (body[1]) {
                case 'n': return '\n';
                case 't': return '\t';
                case 'r': return '\r';
                case '0': return 0;
                case 'e': return 27;
                case '\\': return '\\';
                case '\'': return '\'';
                case '"': return '"';
            }
        }
        if (body.size() == 4 && body[1] == 'x') {
            int value = 0;
            auto [end, ec] = std::from_chars(body.data() + 2, body.data() + 4, value, 16);
            if (ec == std::errc() && end == body.data() + 4) return value;
        }
        throw bad("unknown escape " + std::string(t.text));
    }

    std::int64_t parse_int_literal(const std::string& context) {
        if (peek().type == Tok::Char) return parse_char_literal();
        bool negative = false;
        if (peek_op("-")) {
            const Token& minus = advance();
            const Token& digits = peek();
            if (digits.type != Tok::Int || digits.offset != minus.offset + 1) {
                throw error(minus, "expected an integer after '-'");
            }
            negative = true;
        }
        const Token& t = expect(Tok::Int, context);
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
        // Positive literals may use the full unsigned range (addresses such as 0xffffffff80000000).
        const bool too_negative = negative && magnitude > (std::uint64_t{1} << 63);
        if (ec != std::errc() || end != text.data() + text.size() || too_negative) {
            throw error(t, "integer literal '" + std::string(t.text) + "' does not fit in 64 bits");
        }
        return negative ? static_cast<std::int64_t>(0 - magnitude) : static_cast<std::int64_t>(magnitude);
    }

    Op make(OpKind kind, const Token& t) {
        Op op;
        op.kind = kind;
        op.loc = loc_of(file_, t);
        op.text = std::string(t.text);
        return op;
    }

    Op parse_op() {
        const Token& t = peek();
        switch (t.type) {
            case Tok::Int:
            case Tok::Char: {
                Op op = make(OpKind::PushInt, t);
                op.value = parse_int_literal("");
                return op;
            }
            case Tok::Op: {
                // `-5` (no space) is a negative literal; a lone `-` is subtraction.
                if (t.text == "-" && peek(1).type == Tok::Int && peek(1).offset == t.offset + 1) {
                    Op op = make(OpKind::PushInt, t);
                    op.value = parse_int_literal("");
                    return op;
                }
                advance();
                return make(symbol_ops().at(std::string(t.text)), t);
            }
            case Tok::True:
            case Tok::False: {
                advance();
                Op op = make(OpKind::PushBool, t);
                op.value = t.type == Tok::True;
                return op;
            }
            case Tok::String: {
                advance();
                Op op = make(OpKind::PushString, t);
                op.text = std::string(t.text.substr(1, t.text.size() - 2));
                return op;
            }
            case Tok::Ident: {
                if (t.text == "as" && peek(1).type == Tok::Ident &&
                    (peek(1).text == "int" || peek(1).text == "bool" || peek(1).text == "ptr")) {
                    throw old_as_error(t);
                }
                advance();
                if (peek().type == Tok::Brackets && peek().offset == t.offset + t.text.size()) {
                    advance();
                    return make(OpKind::Index, t);  // name[]: the address of an element
                }
                auto builtin = builtin_words().find(std::string(t.text));
                return make(builtin != builtin_words().end() ? builtin->second : OpKind::Name, t);
            }
            case Tok::Amp: {
                advance();
                const Token& name = peek();
                if (name.type != Tok::Ident || name.offset != t.offset + 1) {
                    throw error(t, "expected a stackexpr name right after '&', such as &handler");
                }
                advance();
                Op op = make(OpKind::AddrOf, t);
                op.text = std::string(name.text);
                return op;
            }
            case Tok::Sizeof: {
                advance();
                const Token& name = expect(Tok::Ident, "after 'sizeof' (a struct name)");
                Op op = make(OpKind::SizeOf, t);
                op.text = std::string(name.text);
                return op;
            }
            case Tok::Lengthof: {
                advance();
                const Token& name = expect(Tok::Ident, "after 'lengthof' (an array name)");
                Op op = make(OpKind::LengthOf, t);
                op.text = std::string(name.text);
                return op;
            }
            case Tok::Brackets:
                throw error(t, "'[]' must follow an array name directly, as in: index buffer[]");
            case Tok::Call: {
                advance();
                const Token& name = expect(Tok::Ident, "after 'call'");
                Op op = make(OpKind::Call, t);
                op.text = std::string(name.text);
                return op;
            }
            case Tok::Execute: {
                advance();
                Op op = make(OpKind::Execute, t);
                if (peek().type != Tok::LParen) {
                    throw error(peek(), "'execute' needs the stack effect of what it calls, such as execute (int -- int)");
                }
                op.sig = parse_signature();
                return op;
            }
            case Tok::Invoke: {
                advance();
                const Token& s = expect(Tok::String, "after 'invoke'");
                Op op = make(OpKind::Asm, t);
                op.text = std::string(s.text.substr(1, s.text.size() - 2));
                if (peek().type == Tok::LParen) op.sig = parse_signature();
                return op;
            }
            case Tok::Tilde: {
                advance();
                Op op = make(OpKind::As, t);
                op.type = parse_conversion(t);
                op.text = "~" + to_string(op.type);
                return op;
            }
            case Tok::If: {
                advance();
                Op op = make(OpKind::If, t);
                op.body = parse_block({Tok::Else, Tok::End}, "'if'");
                if (peek().type == Tok::Else) {
                    advance();
                    op.has_else = true;
                    op.else_body = parse_block({Tok::End}, "'else'");
                }
                advance();  // end
                return op;
            }
            case Tok::While: {
                advance();
                Op op = make(OpKind::While, t);
                op.cond = parse_block({Tok::Do, Tok::End}, "'while'");
                expect(Tok::Do, "after the 'while' condition");
                op.body = parse_block({Tok::End}, "'while'");
                advance();  // end
                return op;
            }
            case Tok::Break: advance(); return make(OpKind::Break, t);
            case Tok::Continue: advance(); return make(OpKind::Continue, t);
            case Tok::Return: advance(); return make(OpKind::Return, t);
            case Tok::Let: {
                advance();
                Op op = make(OpKind::Let, t);
                while (peek().type == Tok::Ident) op.names.push_back(std::string(advance().text));
                if (op.names.empty()) throw error(peek(), "expected names after 'let', such as: let x y in ... end");
                expect(Tok::In, "after the names in 'let'");
                op.body = parse_block({Tok::End}, "'let'");
                advance();  // end
                return op;
            }
            case Tok::Set: {
                advance();
                const Token& name = expect(Tok::Ident, "after 'set' (a local name)");
                Op op = make(OpKind::Set, t);
                op.text = std::string(name.text);
                return op;
            }

            case Tok::StackExpr:
            case Tok::Inline:
            case Tok::Interrupt:
                throw error(t, "definitions cannot be nested; missing 'end' before this?");
            case Tok::Grab:
            case Tok::Extern:
            case Tok::Const:
            case Tok::Var:
            case Tok::Data:
            case Tok::Reserve:
            case Tok::Struct:
                throw error(t, std::string(describe(t.type)) + " is only allowed at top level");
            case Tok::Do:
                throw error(t, "'do' without a matching 'while'");
            case Tok::Else:
                throw error(t, "'else' without a matching 'if'");
            case Tok::In:
                throw error(t, "'in' without a matching 'let'");
            case Tok::Float:
                throw error(t, "floating point numbers are not supported");
            case Tok::Error:
                throw error(t, "invalid input '" + std::string(t.text) + "'");
            case Tok::LParen:
            case Tok::RParen:
            case Tok::DashDash:
            case Tok::Colon:
                throw error(t, std::string("unexpected ") + describe(t.type) +
                                   "; stack effects go right after a stackexpr's name");
            case Tok::End:
            case Tok::Eof:
                break;
        }
        throw error(t, std::string("unexpected ") + describe(t.type));
    }
};

class Loader {
public:
    Loader(Program& prog, fs::path lib_dir, std::vector<SourceFile>& files)
        : prog_(prog), lib_dir_(std::move(lib_dir)), files_(files) {}

    void load_file(const fs::path& path, const Loc* from) {
        std::error_code ec;
        fs::path canonical = fs::weakly_canonical(path, ec);
        std::string key = ec ? path.string() : canonical.string();
        if (!loaded_.insert(key).second) return;

        std::ifstream in(path, std::ios::binary);
        if (!in) {
            if (from) throw CompileError(*from, "cannot open '" + path.string() + "'");
            throw std::runtime_error("cannot open '" + path.string() + "'");
        }
        std::stringstream buf;
        buf << in.rdbuf();

        files_.push_back({path.string(), buf.str()});
        auto index = static_cast<std::uint32_t>(files_.size() - 1);
        // Copy the text: files_ may reallocate while grabs are loaded, and the tokens
        // point into it only for the duration of this parse.
        const std::string source = files_.back().text;
        std::vector<Token> tokens = lex(source);
        Grabs grabs = Parser(tokens, index, prog_, files_).run();

        for (const auto& [name, loc] : grabs.asm_libs) add_asm(name, loc);
        for (const auto& [rel, loc] : grabs.files) load_file(path.parent_path() / rel, &loc);
    }

private:
    Program& prog_;
    fs::path lib_dir_;
    std::vector<SourceFile>& files_;
    std::set<std::string> loaded_;
    std::set<std::string> asm_loaded_;

    void add_asm(const std::string& name, Loc loc) {
        if (!asm_loaded_.insert(name).second) return;
        if (!fs::exists(lib_dir_ / (name + ".asm"))) {
            throw CompileError(loc, "library asm<" + name + "> not found in " + lib_dir_.string());
        }
        prog_.asm_libs.push_back({name, loc});
        fs::path decls = lib_dir_ / (name + ".sxp");
        if (fs::exists(decls)) load_file(decls, &loc);
    }
};

}  // namespace

Program load_program(const std::string& path, const fs::path& lib_dir, std::vector<SourceFile>& files) {
    Program prog;
    Loader(prog, lib_dir, files).load_file(path, nullptr);
    if (prog.stackexprs.find("main") == prog.stackexprs.end()) {
        throw CompileError(Loc{0, 1, 1}, "no entry point; define one with 'stackexpr main ... end'");
    }
    return prog;
}

}  // namespace stackexpr

#include "typecheck.hpp"

#include "parser.hpp"

#include <algorithm>
#include <map>
#include <tuple>
#include <optional>
#include <set>
#include <sstream>

namespace stackexpr {

namespace {

using Stack = std::vector<Type>;

// Parses a signature written as "int ptr -- bool" (types or single-letter variables).
Signature sig(const std::string& text) {
    Signature s;
    bool out = false;
    std::istringstream in(text);
    std::string word;
    while (in >> word) {
        if (word == "--") {
            out = true;
        } else {
            Type t = word == "int" ? Type::int_() : word == "bool" ? Type::bool_() : word == "ptr" ? Type::ptr() : Type::variable(word);
            (out ? s.out : s.in).push_back(t);
        }
    }
    return s;
}

// The stack effects of built-in words. Overloaded words list every variant; the first
// that matches the stack is used, and its index is recorded in Op::overload.
const std::vector<Signature>& builtin_overloads(OpKind kind) {
    static const std::map<OpKind, std::vector<Signature>> table = [] {
        std::map<OpKind, std::vector<Signature>> t;
        auto ints2 = std::vector<Signature>{sig("int int -- int")};
        t[OpKind::Add] = {sig("int int -- int"), sig("ptr int -- ptr"), sig("int ptr -- ptr")};
        t[OpKind::Sub] = {sig("int int -- int"), sig("ptr int -- ptr"), sig("ptr ptr -- int")};
        for (OpKind k : {OpKind::Mul, OpKind::Div, OpKind::Mod, OpKind::UDiv, OpKind::UMod,
                         OpKind::Shl, OpKind::Shr, OpKind::Sar}) {
            t[k] = ints2;
        }
        t[OpKind::Inc] = {sig("int -- int"), sig("ptr -- ptr")};
        for (OpKind k : {OpKind::Lt, OpKind::Gt, OpKind::Le, OpKind::Ge}) t[k] = {sig("int int -- bool")};
        for (OpKind k : {OpKind::ULt, OpKind::UGt, OpKind::ULe, OpKind::UGe}) {
            t[k] = {sig("int int -- bool"), sig("ptr ptr -- bool")};
        }
        for (OpKind k : {OpKind::Eq, OpKind::Ne}) t[k] = {sig("a a -- bool")};
        for (OpKind k : {OpKind::And, OpKind::Or, OpKind::Xor}) {
            t[k] = {sig("int int -- int"), sig("bool bool -- bool")};
        }
        t[OpKind::Not] = {sig("int -- int"), sig("bool -- bool")};
        for (OpKind k : {OpKind::Load8, OpKind::Load16, OpKind::Load32, OpKind::Load64}) t[k] = {sig("ptr -- int")};
        for (OpKind k : {OpKind::Store8, OpKind::Store16, OpKind::Store32, OpKind::Store64}) {
            t[k] = {sig("a ptr --")};
        }
        t[OpKind::Dup] = {sig("a -- a a")};
        t[OpKind::Drop] = {sig("a --")};
        t[OpKind::Swap] = {sig("a b -- b a")};
        t[OpKind::Over] = {sig("a b -- a b a")};
        t[OpKind::Rot] = {sig("a b c -- b c a")};
        t[OpKind::Nip] = {sig("a b -- b")};
        t[OpKind::Tuck] = {sig("a b -- b a b")};
        for (OpKind k : {OpKind::InB, OpKind::InW, OpKind::InD}) t[k] = {sig("int -- int")};
        for (OpKind k : {OpKind::OutB, OpKind::OutW, OpKind::OutD}) t[k] = {sig("int int --")};
        for (OpKind k : {OpKind::Cli, OpKind::Sti, OpKind::Hlt}) t[k] = {sig("--")};
        for (OpKind k : {OpKind::Lgdt, OpKind::Lidt, OpKind::Invlpg}) t[k] = {sig("ptr --")};
        t[OpKind::RdMsr] = {sig("int -- int")};
        t[OpKind::WrMsr] = {sig("int int --")};
        for (OpKind k : {OpKind::RdCr0, OpKind::RdCr2, OpKind::RdCr3, OpKind::RdCr4}) t[k] = {sig("-- int")};
        for (OpKind k : {OpKind::WrCr0, OpKind::WrCr3, OpKind::WrCr4}) t[k] = {sig("int --")};
        return t;
    }();
    static const std::vector<Signature> none;
    auto it = table.find(kind);
    return it == table.end() ? none : it->second;
}

std::string q(const std::string& s) { return "'" + s + "'"; }

std::string show(const Stack& s) { return "(" + to_string(s) + ")"; }

// The top `n` values of `s`, or all of it when shorter.
Stack top(const Stack& s, std::size_t n) {
    return n >= s.size() ? s : Stack(s.end() - static_cast<std::ptrdiff_t>(n), s.end());
}

// Tries to apply `sig` to the top of `s`. Type variables in `sig` are bound to whatever
// they match; a variable used twice must match the same type both times.
std::optional<Stack> try_apply(const Signature& sig, const Stack& s) {
    if (s.size() < sig.in.size()) return std::nullopt;
    std::map<std::string, Type> bound;
    std::size_t base = s.size() - sig.in.size();
    for (std::size_t i = 0; i < sig.in.size(); ++i) {
        const Type& want = sig.in[i];
        const Type& have = s[base + i];
        if (want.kind == TypeKind::Var) {
            auto [it, inserted] = bound.emplace(want.var, have);
            if (!inserted && it->second != have) return std::nullopt;
        } else if (want != have) {
            return std::nullopt;
        }
    }
    Stack result(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(base));
    for (const Type& t : sig.out) result.push_back(t.kind == TypeKind::Var ? bound.at(t.var) : t);
    return result;
}

void check_signature(const Signature& s, Loc loc, const std::string& what) {
    std::set<std::string> inputs;
    for (const Type& t : s.in) {
        if (t.kind == TypeKind::Var) inputs.insert(t.var);
    }
    for (const Type& t : s.out) {
        if (t.kind == TypeKind::Var && !inputs.count(t.var)) {
            throw CompileError(loc, what + " outputs type '" + t.var +
                                        "', which doesn't appear in its inputs, so its type is unknown");
        }
    }
}

class Checker {
public:
    explicit Checker(Program& prog) : prog_(prog) {}

    // Checks everything, collecting at most one error per definition so a single
    // mistake doesn't hide the others.
    std::vector<CompileError> run() {
        std::vector<CompileError> errors;
        auto attempt = [&](auto&& check) {
            try {
                check();
            } catch (const CompileError& e) {
                errors.push_back(e);
            }
        };
        for (auto& [name, def] : prog_.externs) {
            attempt([&, &name = name, &def = def] { check_signature(def.sig, def.loc, "extern " + q(name)); });
        }
        for (auto& [name, def] : prog_.globals) {
            attempt([&, &def = def] { resolve_reserve(def); });
        }
        std::set<std::string> bad_signature;
        for (auto& [name, def] : prog_.stackexprs) {
            attempt([&, &name = name, &def = def] {
                try {
                    check_stackexpr_signature(name, def);
                } catch (...) {
                    bad_signature.insert(name);
                    throw;
                }
            });
        }
        for (auto& [name, def] : prog_.stackexprs) {
            if (!bad_signature.count(name)) attempt([&, &def = def] { check_definition(def); });
        }
        return errors;
    }

private:
    void check_stackexpr_signature(const std::string& name, const StackExprDef& def) {
        check_signature(def.sig, def.loc, "stackexpr " + q(name));
        if (name == "main" && (!def.sig.in.empty() || !def.sig.out.empty())) {
            throw CompileError(def.loc, "'main' must have the stack effect ( -- ); it starts and ends with an empty stack");
        }
        if (def.kind == ExprKind::Interrupt) {
            bool plain = def.sig.in.empty();
            bool error_code = def.sig.in.size() == 1 && def.sig.in[0] == Type::int_();
            if (!def.sig.out.empty() || !(plain || error_code)) {
                throw CompileError(def.loc, "interrupt handlers have the stack effect ( -- ), or (int -- ) "
                                            "for CPU exceptions that push an error code");
            }
        }
    }

    // Works out a reserve's element count and size, which may name consts and structs
    // defined anywhere.
    void resolve_reserve(GlobalDef& g) {
        if (g.kind != GlobalKind::Reserve) return;
        if (!g.count_name.empty()) {
            auto c = prog_.consts.find(g.count_name);
            if (c == prog_.consts.end() || c->second.type != Type::int_()) {
                throw CompileError(g.count_loc, "reserve " + q(g.name) + ": " + q(g.count_name) +
                                                    " is not an int const; the size must be a number or an int const");
            }
            g.count = c->second.value;
            if (g.count <= 0) throw CompileError(g.count_loc, "reserve " + q(g.name) + ": size must be positive");
        }
        if (g.is_array()) {
            static const std::map<std::string, std::int64_t> scalars = {
                {"u8", 1}, {"u16", 2}, {"u32", 4}, {"u64", 8}, {"int", 8}, {"ptr", 8}, {"bool", 8},
            };
            if (auto sc = scalars.find(g.elem_type); sc != scalars.end()) {
                g.elem_size = sc->second;
            } else if (auto st = prog_.structs.find(g.elem_type); st != prog_.structs.end()) {
                g.elem_size = st->second.size;
                if (g.elem_size == 0) throw CompileError(g.elem_loc, "struct " + q(g.elem_type) + " is empty");
            } else {
                throw CompileError(g.elem_loc, "unknown element type " + q(g.elem_type) +
                                                   "; use u8, u16, u32, u64, int, ptr, bool or a struct");
            }
        }
        if (g.count > (std::int64_t{1} << 40) / g.elem_size) {
            throw CompileError(g.count_loc, "reserve " + q(g.name) + " is too large");
        }
        g.size = g.count * g.elem_size;
    }

    const GlobalDef* find_array(const Op& op) const {
        auto it = prog_.globals.find(op.text);
        if (it == prog_.globals.end() || !it->second.is_array()) {
            std::string is = what_is(op.text);
            throw CompileError(op.loc, q(op.text) + " is " + (is.empty() ? std::string("not defined") : is) +
                                           ", not an array; declare one with: reserve " + op.text + " COUNT TYPE");
        }
        return &it->second;
    }

    struct Local {
        std::string name;
        std::int64_t slot;
        Type type;
    };

    Program& prog_;
    StackExprDef* def_ = nullptr;
    std::vector<Stack> loops_;          // the stack at the start of each enclosing while
    std::vector<Local> locals_;         // innermost last
    std::int64_t next_slot_ = 0;

    std::string kind_name() const {
        return def_->kind == ExprKind::Interrupt ? "interrupt " + q(def_->name) : "stackexpr " + q(def_->name);
    }

    void check_definition(StackExprDef& def) {
        def_ = &def;
        loops_.clear();
        locals_.clear();
        next_slot_ = 0;
        std::optional<Stack> end = check_block(def.body, def.sig.in);
        if (end && *end != def.sig.out) {
            throw CompileError(def.loc, kind_name() + " is declared " + to_string(def.sig) + " but leaves " +
                                            show(*end) + " at its end");
        }
        def.frame_slots = next_slot_;
    }

    std::optional<Stack> check_block(std::vector<Op>& ops, std::optional<Stack> s) {
        const Op* prev = nullptr;
        for (Op& op : ops) {
            if (!s) {
                throw CompileError(op.loc, "unreachable code: the previous 'return', 'break' or 'continue' "
                                           "always leaves this block");
            }
            s = check_op(op, std::move(*s));
            if (op.kind == OpKind::Index && prev && is_constant_int(*prev)) check_constant_index(op, *prev);
            prev = &op;
        }
        return s;
    }

    static bool is_constant_int(const Op& op) {
        return op.kind == OpKind::PushInt || (op.kind == OpKind::Name && op.ref == NameRef::Const);
    }

    // An index known at compile time is checked now, and needs no check at run time.
    void check_constant_index(Op& op, const Op& index) {
        std::uint64_t i = static_cast<std::uint64_t>(index.value);
        if (i >= static_cast<std::uint64_t>(op.count)) {
            throw CompileError(index.loc, "index " + std::to_string(index.value) + " is out of bounds for " + q(op.text) +
                                              ", which has " + std::to_string(op.count) + " elements (0 to " +
                                              std::to_string(op.count - 1) + ")");
        }
        op.checked = true;
    }

    Stack apply(const Signature& sig, const Stack& s, Loc loc, const std::string& what) {
        if (auto result = try_apply(sig, s)) return *result;
        throw CompileError(loc, what + " expects (" + to_string(sig.in) + ") on top of the stack, but found " +
                                    show(top(s, sig.in.size())));
    }

    Stack apply_builtin(Op& op, const Stack& s) {
        const std::vector<Signature>& options = builtin_overloads(op.kind);
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (auto result = try_apply(options[i], s)) {
                op.overload = static_cast<int>(i);
                return *result;
            }
        }
        std::size_t want = options.empty() ? 0 : options[0].in.size();
        std::string expected;
        for (const Signature& o : options) {
            if (!expected.empty()) expected += " or ";
            expected += "(" + to_string(o.in) + ")";
        }
        throw CompileError(op.loc, q(op.text) + " expects " + expected + " on top of the stack, but found " +
                                       show(top(s, want)));
    }

    const Local* find_local(const std::string& name) const {
        for (auto it = locals_.rbegin(); it != locals_.rend(); ++it) {
            if (it->name == name) return &*it;
        }
        return nullptr;
    }

    bool is_callable(const std::string& name) const {
        return prog_.stackexprs.count(name) || prog_.externs.count(name);
    }

    std::string what_is(const std::string& name) const {
        if (find_local(name)) return "a local";
        if (prog_.consts.count(name)) return "a const";
        if (auto g = prog_.globals.find(name); g != prog_.globals.end()) {
            return g->second.kind == GlobalKind::Var    ? "a var"
                   : g->second.kind == GlobalKind::Data ? "a data"
                   : g->second.is_array()               ? "an array"
                                                        : "a reserve";
        }
        if (prog_.structs.count(name)) return "a struct";
        if (prog_.externs.count(name)) return "an extern";
        if (prog_.stackexprs.count(name)) return "a stackexpr";
        return {};
    }

    std::optional<Stack> check_op(Op& op, Stack s) {
        switch (op.kind) {
            case OpKind::PushInt:
                s.push_back(Type::int_());
                return s;
            case OpKind::PushBool:
                s.push_back(Type::bool_());
                return s;
            case OpKind::PushString:
                s.push_back(Type::ptr());
                return s;

            case OpKind::Name: return check_name(op, std::move(s));

            case OpKind::AddrOf: {
                auto it = prog_.stackexprs.find(op.text);
                if (it == prog_.stackexprs.end()) {
                    std::string is = what_is(op.text);
                    throw CompileError(op.loc, "'&" + op.text + "': " +
                                                   (is.empty() ? "no stackexpr named " + q(op.text)
                                                               : q(op.text) + " is " + is + ", not a stackexpr"));
                }
                if (it->second.kind == ExprKind::Inline) {
                    throw CompileError(op.loc, "inline stackexpr " + q(op.text) + " has no address; remove 'inline' to use &" + op.text);
                }
                s.push_back(Type::ptr());
                return s;
            }

            case OpKind::SizeOf: {
                if (auto st = prog_.structs.find(op.text); st != prog_.structs.end()) {
                    op.value = st->second.size;
                } else if (auto g = prog_.globals.find(op.text);
                           g != prog_.globals.end() && g->second.kind == GlobalKind::Reserve) {
                    op.value = g->second.size;
                } else {
                    throw CompileError(op.loc, "'sizeof' needs a struct or a reserve, but " + q(op.text) + " is " +
                                                   (what_is(op.text).empty() ? std::string("not defined") : what_is(op.text)));
                }
                s.push_back(Type::int_());
                return s;
            }

            case OpKind::LengthOf:
                op.value = find_array(op)->count;
                s.push_back(Type::int_());
                return s;

            case OpKind::Index: {
                const GlobalDef* g = find_array(op);
                op.value = g->elem_size;
                op.count = g->count;
                return apply(sig("int -- ptr"), s, op.loc, q(op.text + "[]"));
            }

            case OpKind::Call: {
                if (auto it = prog_.stackexprs.find(op.text); it != prog_.stackexprs.end()) {
                    const StackExprDef& callee = it->second;
                    if (callee.name == "main") throw CompileError(op.loc, "'main' is the entry point and cannot be called");
                    if (callee.kind == ExprKind::Interrupt) {
                        throw CompileError(op.loc, q(op.text) + " is an interrupt handler; it can't be called, "
                                                   "only installed with &" + op.text);
                    }
                    return apply(callee.sig, s, op.loc, "'call " + op.text + "'");
                }
                if (auto it = prog_.externs.find(op.text); it != prog_.externs.end()) {
                    return apply(it->second.sig, s, op.loc, "'call " + op.text + "'");
                }
                std::string is = what_is(op.text);
                if (!is.empty()) {
                    throw CompileError(op.loc, q(op.text) + " is " + is + "; write just '" + op.text + "' to push it");
                }
                throw CompileError(op.loc, "unknown stackexpr " + q(op.text) +
                                               (prog_.asm_libs.empty() ? "" : "; assembly macros must be declared with 'extern'"));
            }

            case OpKind::Execute: {
                if (s.empty() || s.back() != Type::ptr()) {
                    throw CompileError(op.loc, "'execute' expects the address to call (ptr) on top of the stack, but found " +
                                                   show(top(s, 1)));
                }
                s.pop_back();
                check_signature(op.sig, op.loc, "'execute'");
                return apply(op.sig, s, op.loc, "'execute " + to_string(op.sig) + "'");
            }

            case OpKind::Asm:
                check_signature(op.sig, op.loc, "'invoke'");
                return apply(op.sig, s, op.loc, "'invoke'");

            case OpKind::As: {
                if (s.empty()) throw CompileError(op.loc, q(op.text) + " needs a value on the stack");
                // Converting a non-bool to bool normalises it to 0 or 1.
                op.overload = op.type == Type::bool_() && s.back() != Type::bool_() ? 1 : 0;
                s.back() = op.type;
                return s;
            }

            case OpKind::If: {
                if (s.empty() || s.back() != Type::bool_()) {
                    throw CompileError(op.loc, "'if' expects a bool on top of the stack, but found " + show(top(s, 1)) +
                                                   "; compare first (e.g. '0 !=') or convert with '~bool'");
                }
                s.pop_back();
                std::optional<Stack> then_end = check_block(op.body, s);
                std::optional<Stack> else_end = op.has_else ? check_block(op.else_body, s) : std::optional<Stack>(s);
                if (!then_end) return else_end;
                if (!else_end) return then_end;
                if (*then_end != *else_end) {
                    if (!op.has_else) {
                        throw CompileError(op.loc, "an 'if' without 'else' must leave the stack as it found it, but it "
                                                   "changes " + show(s) + " to " + show(*then_end));
                    }
                    throw CompileError(op.loc, "the two branches of this 'if' leave different stacks: " +
                                                   show(*then_end) + " and " + show(*else_end));
                }
                return then_end;
            }

            case OpKind::While: {
                Stack entry = s;
                std::optional<Stack> cond = check_block(op.cond, entry);
                Stack expected = entry;
                expected.push_back(Type::bool_());
                if (!cond || *cond != expected) {
                    throw CompileError(op.loc, "a 'while' condition must push one bool and leave the rest of the stack "
                                               "alone: expected " + show(expected) + " before 'do', found " +
                                                   (cond ? show(*cond) : std::string("nothing (it never finishes)")));
                }
                loops_.push_back(entry);
                std::optional<Stack> body_end = check_block(op.body, entry);
                loops_.pop_back();
                if (body_end && *body_end != entry) {
                    throw CompileError(op.loc, "a 'while' body must leave the stack as it found it, but it changes " +
                                                   show(entry) + " to " + show(*body_end));
                }
                return entry;
            }

            case OpKind::Break:
            case OpKind::Continue: {
                if (loops_.empty()) throw CompileError(op.loc, q(op.text) + " is only allowed inside a 'while' body");
                if (s != loops_.back()) {
                    throw CompileError(op.loc, q(op.text) + " must leave the stack as it was at the start of the loop, " +
                                                   show(loops_.back()) + ", but it is " + show(s));
                }
                return std::nullopt;
            }

            case OpKind::Return: {
                if (def_->kind == ExprKind::Inline) {
                    throw CompileError(op.loc, "'return' is not allowed in an inline stackexpr");
                }
                if (s != def_->sig.out) {
                    throw CompileError(op.loc, "'return' in " + kind_name() + " must leave (" + to_string(def_->sig.out) +
                                                   "), as declared, but the stack is " + show(s));
                }
                return std::nullopt;
            }

            case OpKind::Let: {
                if (def_->kind == ExprKind::Inline) {
                    throw CompileError(op.loc, "'let' is not allowed in an inline stackexpr; remove 'inline'");
                }
                if (s.size() < op.names.size()) {
                    throw CompileError(op.loc, "'let' binds " + std::to_string(op.names.size()) +
                                                   " values but the stack has " + show(s));
                }
                std::size_t base = s.size() - op.names.size();
                std::size_t scope = locals_.size();
                op.slots.clear();
                for (std::size_t i = 0; i < op.names.size(); ++i) {
                    const std::string& name = op.names[i];
                    if (is_reserved_name(name)) throw CompileError(op.loc, q(name) + " is a built-in word and can't be a local");
                    std::string is = what_is(name);
                    if (!is.empty()) throw CompileError(op.loc, "local " + q(name) + " would hide " + is + " with the same name");
                    for (std::size_t j = 0; j < i; ++j) {
                        if (op.names[j] == name) throw CompileError(op.loc, "'let' names " + q(name) + " twice");
                    }
                    op.slots.push_back(next_slot_);
                    locals_.push_back({name, next_slot_++, s[base + i]});
                }
                s.resize(base);
                std::optional<Stack> end = check_block(op.body, s);
                locals_.resize(scope);
                return end;
            }

            case OpKind::Set: {
                const Local* local = find_local(op.text);
                if (!local) throw CompileError(op.loc, "'set " + op.text + "': no local named " + q(op.text));
                if (s.empty() || s.back() != local->type) {
                    throw CompileError(op.loc, "'set " + op.text + "' expects a " + to_string(local->type) +
                                                   " on top of the stack, but found " + show(top(s, 1)));
                }
                op.value = local->slot;
                s.pop_back();
                return s;
            }

            default:
                return apply_builtin(op, s);
        }
    }

    std::optional<Stack> check_name(Op& op, Stack s) {
        if (const Local* local = find_local(op.text)) {
            op.ref = NameRef::Local;
            op.value = local->slot;
            s.push_back(local->type);
            return s;
        }
        if (auto it = prog_.consts.find(op.text); it != prog_.consts.end()) {
            op.ref = NameRef::Const;
            op.value = it->second.value;
            s.push_back(it->second.type);
            return s;
        }
        if (prog_.globals.count(op.text)) {
            op.ref = NameRef::Global;
            s.push_back(Type::ptr());
            return s;
        }
        // Struct.field: ptr -- ptr, adding the field's offset.
        if (auto dot = op.text.rfind('.'); dot != std::string::npos) {
            auto st = prog_.structs.find(op.text.substr(0, dot));
            if (st != prog_.structs.end()) {
                std::string field = op.text.substr(dot + 1);
                for (const Field& f : st->second.fields) {
                    if (f.name == field) {
                        op.ref = NameRef::Field;
                        op.value = f.offset;
                        return apply(sig("ptr -- ptr"), s, op.loc, q(op.text));
                    }
                }
                throw CompileError(op.loc, "struct " + q(st->first) + " has no field " + q(field));
            }
        }
        if (is_callable(op.text)) {
            throw CompileError(op.loc, q(op.text) + " is a stackexpr; use 'call " + op.text + "' to run it, or '&" +
                                           op.text + "' for its address");
        }
        if (prog_.structs.count(op.text)) {
            throw CompileError(op.loc, q(op.text) + " is a struct; use 'sizeof " + op.text + "' or a field such as " +
                                           op.text + ".name");
        }
        throw CompileError(op.loc, "unknown name " + q(op.text));
    }
};

}  // namespace

void typecheck(Program& prog) {
    std::vector<CompileError> errors = Checker(prog).run();
    std::stable_sort(errors.begin(), errors.end(), [](const CompileError& a, const CompileError& b) {
        return std::tie(a.loc.file, a.loc.line, a.loc.column) < std::tie(b.loc.file, b.loc.line, b.loc.column);
    });
    if (!errors.empty()) throw CompileErrors(std::move(errors));
}

}  // namespace stackexpr

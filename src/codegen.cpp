#include "codegen.hpp"

#include <set>
#include <sstream>
#include <vector>

namespace stackexpr {

namespace {

class Generator {
public:
    explicit Generator(const Program& prog) : prog_(prog) {
        for (const ConstDef& c : prog.consts) {
            if (!const_names_.insert(c.name).second) {
                throw CompileError(c.loc, "const '" + c.name + "' is already defined");
            }
            if (prog.stackexprs.count(c.name)) {
                throw CompileError(c.loc, "'" + c.name + "' is defined as both a const and a stackexpr");
            }
        }
    }

    std::string run() {
        out_ << "bits 64\n"
             << "default rel\n\n"
             << "section .text\n"
             << "global _start\n";

        for (const Include& inc : prog_.includes) {
            out_ << "\n;; --- grab asm<" << inc.lib << "> ---\n"
                 << "%include \"" << inc.lib << ".asm\"\n";
        }

        const StackExprDef& main = prog_.stackexprs.at("main");
        out_ << "\n_start:\n";
        expanding_.push_back(&main);
        emit_block(main.body);
        expanding_.pop_back();

        out_ << "\n;; --- exit(0) ---\n"
             << "    mov rax, 60\n"
             << "    xor rdi, rdi\n"
             << "    syscall\n";

        // Storage required by lib/io.asm.
        out_ << "\nsection .bss\n"
             << "    digitSpace resb 100\n"
             << "    digitSpacePos resb 8\n";

        out_ << "\nsection .data\n"
             << "    negative_symbol db '-', 0\n";
        for (const ConstDef& c : prog_.consts) {
            if (c.is_string) {
                out_ << "    " << c.name << " db " << nasm_string(c.string_value) << ", 0\n";
            } else {
                out_ << "    " << c.name << " dq " << c.int_value << "\n";
            }
        }
        return out_.str();
    }

private:
    const Program& prog_;
    std::ostringstream out_;
    std::set<std::string> const_names_;
    std::vector<const StackExprDef*> expanding_;  // for recursion detection
    std::size_t next_label_ = 0;

    // Backquoted NASM strings understand C-style escapes, matching the source syntax.
    static std::string nasm_string(const std::string& s) {
        std::string r = "`";
        for (char c : s) {
            if (c == '`') r += '\\';
            r += c;
        }
        return r + "`";
    }

    void line(const std::string& s) { out_ << "    " << s << "\n"; }
    void comment(const std::string& s) { out_ << "    ;; " << s << "\n"; }

    void binary(const char* what, std::initializer_list<const char*> body) {
        comment(what);
        line("pop rbx");
        line("pop rax");
        for (const char* s : body) line(s);
        line("push rax");
    }

    void compare(const char* what, const char* setcc) {
        binary(what, {"cmp rax, rbx", setcc, "movzx rax, al"});
    }

    void emit_block(const std::vector<Op>& ops) {
        for (const Op& op : ops) emit(op);
    }

    void emit(const Op& op) {
        switch (op.kind) {
            case OpKind::PushInt:
                comment("push " + std::to_string(op.value));
                line("mov rax, " + std::to_string(op.value));
                line("push rax");
                return;
            case OpKind::PushConst:
                if (!const_names_.count(op.text)) {
                    std::string hint = prog_.stackexprs.count(op.text) ? "; did you mean 'call " + op.text + "'?" : "";
                    throw CompileError(op.loc, "unknown name '" + op.text + "'" + hint);
                }
                comment("push address of " + op.text);
                line("lea rax, [" + op.text + "]");
                line("push rax");
                return;
            case OpKind::Add: binary("+", {"add rax, rbx"}); return;
            case OpKind::Sub: binary("-", {"sub rax, rbx"}); return;
            case OpKind::Mul: binary("*", {"imul rax, rbx"}); return;
            case OpKind::Div: binary("/", {"cqo", "idiv rbx"}); return;
            case OpKind::Inc:
                comment("++");
                line("inc qword [rsp]");
                return;
            case OpKind::Eq: compare("==", "sete al"); return;
            case OpKind::Ne: compare("!=", "setne al"); return;
            case OpKind::Lt: compare("<", "setl al"); return;
            case OpKind::Gt: compare(">", "setg al"); return;
            case OpKind::Le: compare("<=", "setle al"); return;
            case OpKind::Ge: compare(">=", "setge al"); return;
            case OpKind::Dup:
                comment("dup");
                line("push qword [rsp]");
                return;
            case OpKind::Swap:
                comment("swap");
                line("pop rax");
                line("pop rbx");
                line("push rax");
                line("push rbx");
                return;
            case OpKind::Drop:
                comment("drop");
                line("add rsp, 8");
                return;
            case OpKind::Asm:
                comment("invoke");
                line(op.text);
                return;
            case OpKind::Call: emit_call(op); return;
            case OpKind::If: {
                std::size_t id = next_label_++;
                comment("if");
                line("pop rax");
                line("test rax, rax");
                line("jz _sx_if_" + std::to_string(id) + "_end");
                emit_block(op.body);
                out_ << "_sx_if_" << id << "_end:\n";
                return;
            }
            case OpKind::While: {
                std::size_t id = next_label_++;
                out_ << "_sx_while_" << id << ":\n";
                emit_block(op.cond);
                comment("do");
                line("pop rax");
                line("test rax, rax");
                line("jz _sx_while_" + std::to_string(id) + "_end");
                emit_block(op.body);
                line("jmp _sx_while_" + std::to_string(id));
                out_ << "_sx_while_" << id << "_end:\n";
                return;
            }
        }
    }

    void emit_call(const Op& op) {
        auto it = prog_.stackexprs.find(op.text);
        if (it == prog_.stackexprs.end()) {
            if (prog_.includes.empty()) {
                throw CompileError(op.loc, "unknown stackexpr '" + op.text + "'");
            }
            // Assumed to be a macro from a grabbed assembly library; NASM reports it if not.
            comment("call " + op.text + " (asm library)");
            line(op.text);
            return;
        }

        const StackExprDef& def = it->second;
        if (def.name == "main") {
            throw CompileError(op.loc, "'main' is the entry point and cannot be called");
        }
        for (const StackExprDef* active : expanding_) {
            if (active == &def) {
                std::string chain;
                for (const StackExprDef* d : expanding_) chain += d->name + " -> ";
                throw CompileError(op.loc, "recursive stackexpr call: " + chain + def.name +
                                               " (stackexprs are inlined, so recursion is not supported)");
            }
        }

        comment("call " + def.name);
        expanding_.push_back(&def);
        emit_block(def.body);
        expanding_.pop_back();
        comment("end " + def.name);
    }
};

}  // namespace

std::string generate(const Program& prog) {
    return Generator(prog).run();
}

}  // namespace stackexpr

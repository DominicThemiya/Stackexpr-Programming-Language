#include "codegen.hpp"

#include <sstream>
#include <vector>

namespace stackexpr {

namespace {

constexpr std::int64_t kReturnStackSize = 64 * 1024;

// Registers an interrupt handler saves. r15 (the return stack) and rsp are preserved
// by construction.
const char* const kSavedRegs[] = {"rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp",
                                  "r8",  "r9",  "r10", "r11", "r12", "r13", "r14"};
constexpr std::int64_t kSavedRegCount = sizeof(kSavedRegs) / sizeof(kSavedRegs[0]);

std::string fn_label(const std::string& name) { return "sx_fn_" + name; }
std::string int_label(const std::string& name) { return "sx_int_" + name; }
std::string ret_label(const std::string& name) { return "sx_ret_" + name; }
std::string global_label(const std::string& name) { return "sx_g_" + name; }

// Backquoted NASM strings understand C-style escapes, matching the source syntax.
std::string nasm_string(const std::string& s) {
    std::string r = "`";
    for (char c : s) {
        if (c == '`') r += '\\';
        r += c;
    }
    return r + "`";
}

std::string frame_ref(std::int64_t slot) { return "qword [r15 + " + std::to_string(8 * slot) + "]"; }

class Generator {
public:
    Generator(const Program& prog, const CodegenOptions& options) : prog_(prog), options_(options) {}

    std::string run() {
        out_ << "bits 64\n"
             << "default rel\n"
             << "%define SX_ENTRY " << options_.entry << "\n\n"
             << "section .text\n"
             << "global " << options_.entry << "\n";

        for (const AsmLib& lib : prog_.asm_libs) {
            out_ << "\n;; --- grab asm<" << lib.name << "> ---\n"
                 << "%include \"" << lib.name << ".asm\"\n";
        }

        // Libraries may switch sections or modes; return to 64-bit code.
        out_ << "\nsection .text\n"
             << "bits 64\n"
             << options_.entry << ":\n"
             << "    lea r15, [_sx_rstack_top]\n"
             << "    call " << fn_label("main") << "\n";
        if (options_.freestanding) {
            out_ << "_sx_halt:\n"
                 << "    cli\n"
                 << "    hlt\n"
                 << "    jmp _sx_halt\n";
        } else {
            out_ << "    mov eax, 60          ; exit(0)\n"
                 << "    xor edi, edi\n"
                 << "    syscall\n";
        }

        for (const auto& [name, def] : prog_.stackexprs) {
            if (def.kind == ExprKind::Function) emit_function(def);
            if (def.kind == ExprKind::Interrupt) emit_interrupt(def);
        }

        emit_data();
        return out_.str();
    }

private:
    const Program& prog_;
    const CodegenOptions& options_;
    std::ostringstream out_;
    std::size_t next_label_ = 0;
    std::vector<std::string> strings_;
    std::vector<const StackExprDef*> inlining_;  // for recursion detection
    struct Loop {
        std::string start, end;
    };
    std::vector<Loop> loops_;
    const StackExprDef* current_ = nullptr;

    void line(const std::string& s) { out_ << "    " << s << "\n"; }
    void comment(const std::string& s) { out_ << "    ;; " << s << "\n"; }
    void label(const std::string& s) { out_ << s << ":\n"; }
    std::string new_label(const char* kind) { return "_sx_" + std::string(kind) + "_" + std::to_string(next_label_++); }

    // ---- definitions ------------------------------------------------------

    // Frame on the return stack: locals at [r15 + 8*i], the return address after them.
    void emit_function(const StackExprDef& def) {
        current_ = &def;
        std::int64_t n = def.frame_slots;
        out_ << "\n;; --- stackexpr " << def.name << " ---\n";
        label(fn_label(def.name));
        line("sub r15, " + std::to_string(8 * (n + 1)));
        line("pop " + frame_ref(n) + "          ; return address to the return stack");
        emit_block(def.body);
        label(ret_label(def.name));
        line("push " + frame_ref(n));
        line("add r15, " + std::to_string(8 * (n + 1)));
        line("ret");
    }

    // Frame on the return stack: locals at [r15 + 8*i], then the saved registers.
    // The CPU's interrupt frame (and error code, if any) stays on rsp, so an error code
    // is simply the top of the data stack.
    void emit_interrupt(const StackExprDef& def) {
        current_ = &def;
        std::int64_t n = def.frame_slots;
        std::int64_t frame = 8 * (n + kSavedRegCount);
        out_ << "\n;; --- interrupt " << def.name << " ---\n";
        label(int_label(def.name));
        line("sub r15, " + std::to_string(frame));
        for (std::int64_t i = 0; i < kSavedRegCount; ++i) {
            line("mov " + frame_ref(n + i) + ", " + kSavedRegs[i]);
        }
        line("cld");
        emit_block(def.body);
        label(ret_label(def.name));
        for (std::int64_t i = 0; i < kSavedRegCount; ++i) {
            line(std::string("mov ") + kSavedRegs[i] + ", " + frame_ref(n + i));
        }
        line("add r15, " + std::to_string(frame));
        line("iretq");
    }

    void emit_data() {
        bool uses_io = false;
        for (const AsmLib& lib : prog_.asm_libs) uses_io = uses_io || lib.name == "io";

        out_ << "\nsection .rodata\n";
        for (std::size_t i = 0; i < strings_.size(); ++i) {
            out_ << "_sx_str_" << i << ": db " << nasm_string(strings_[i]) << ", 0\n";
        }

        out_ << "\nsection .data\n";
        if (uses_io) out_ << "    negative_symbol db '-', 0   ; for asm<io>\n";
        for (const auto& [name, g] : prog_.globals) {
            if (g.kind == GlobalKind::Var) {
                out_ << "align 8\n" << global_label(name) << ": dq " << g.init << "\n";
            } else if (g.kind == GlobalKind::Data) {
                out_ << global_label(name) << ": db " << nasm_string(g.bytes) << ", 0\n";
            }
        }

        out_ << "\nsection .bss\n";
        if (uses_io) {
            out_ << "    digitSpace resb 100         ; for asm<io>\n"
                 << "    digitSpacePos resb 8\n";
        }
        for (const auto& [name, g] : prog_.globals) {
            if (g.kind == GlobalKind::Reserve) {
                out_ << "alignb " << g.align << "\n" << global_label(name) << ": resb " << g.size << "\n";
            }
        }
        out_ << "alignb 16\n"
             << "_sx_rstack: resb " << kReturnStackSize << "   ; return stack (r15)\n"
             << "_sx_rstack_top:\n";
    }

    // ---- ops --------------------------------------------------------------

    void emit_block(const std::vector<Op>& ops) {
        for (const Op& op : ops) emit(op);
    }

    void push_value(std::int64_t v, const std::string& what) {
        comment(what);
        line("mov rax, " + std::to_string(v));
        line("push rax");
    }

    void push_address(const std::string& lbl, const std::string& what) {
        comment(what);
        line("lea rax, [" + lbl + "]");
        line("push rax");
    }

    void binary(const std::string& what, std::initializer_list<const char*> body) {
        comment(what);
        line("pop rbx");
        line("pop rax");
        for (const char* s : body) line(s);
        line("push rax");
    }

    void compare(const std::string& what, const char* setcc) {
        binary(what, {"cmp rax, rbx", setcc, "movzx eax, al"});
    }

    void load(const std::string& what, const char* insn) {
        comment(what);
        line("pop rax");
        line(insn);
        line("push rax");
    }

    void store(const std::string& what, const char* insn) {
        comment(what);
        line("pop rbx");
        line("pop rax");
        line(insn);
    }

    void port_in(const std::string& what, const char* insn) {
        comment(what);
        line("pop rdx");
        line("xor eax, eax");
        line(insn);
        line("push rax");
    }

    void port_out(const std::string& what, const char* insn) {
        comment(what);
        line("pop rdx");
        line("pop rax");
        line(insn);
    }

    void simple(const std::string& what, std::initializer_list<const char*> body) {
        comment(what);
        for (const char* s : body) line(s);
    }

    void emit(const Op& op) {
        const std::string& w = op.text;
        switch (op.kind) {
            case OpKind::PushInt: push_value(op.value, "push " + std::to_string(op.value)); return;
            case OpKind::PushBool: comment(w); line(op.value ? "push 1" : "push 0"); return;
            case OpKind::PushString:
                strings_.push_back(op.text);
                push_address("_sx_str_" + std::to_string(strings_.size() - 1), "string");
                return;
            case OpKind::Name:
                switch (op.ref) {
                    case NameRef::Local: comment("local " + w); line("push " + frame_ref(op.value)); return;
                    case NameRef::Const: push_value(op.value, "const " + w); return;
                    case NameRef::Global: push_address(global_label(w), w); return;
                    case NameRef::Field: comment(w); line("add qword [rsp], " + std::to_string(op.value)); return;
                    case NameRef::Unresolved: break;
                }
                throw CompileError(op.loc, "internal error: unresolved name '" + w + "'");
            case OpKind::AddrOf: {
                const StackExprDef& d = prog_.stackexprs.at(op.text);
                push_address(d.kind == ExprKind::Interrupt ? int_label(d.name) : fn_label(d.name), "&" + op.text);
                return;
            }
            case OpKind::SizeOf: push_value(op.value, "sizeof " + op.text); return;

            case OpKind::Add: binary(w, {"add rax, rbx"}); return;
            case OpKind::Sub: binary(w, {"sub rax, rbx"}); return;
            case OpKind::Mul: binary(w, {"imul rax, rbx"}); return;
            case OpKind::Div: binary(w, {"cqo", "idiv rbx"}); return;
            case OpKind::Mod: binary(w, {"cqo", "idiv rbx", "mov rax, rdx"}); return;
            case OpKind::UDiv: binary(w, {"xor edx, edx", "div rbx"}); return;
            case OpKind::UMod: binary(w, {"xor edx, edx", "div rbx", "mov rax, rdx"}); return;
            case OpKind::Inc: comment(w); line("inc qword [rsp]"); return;
            case OpKind::Eq: compare(w, "sete al"); return;
            case OpKind::Ne: compare(w, "setne al"); return;
            case OpKind::Lt: compare(w, "setl al"); return;
            case OpKind::Gt: compare(w, "setg al"); return;
            case OpKind::Le: compare(w, "setle al"); return;
            case OpKind::Ge: compare(w, "setge al"); return;
            case OpKind::ULt: compare(w, "setb al"); return;
            case OpKind::UGt: compare(w, "seta al"); return;
            case OpKind::ULe: compare(w, "setbe al"); return;
            case OpKind::UGe: compare(w, "setae al"); return;
            case OpKind::And: binary(w, {"and rax, rbx"}); return;
            case OpKind::Or: binary(w, {"or rax, rbx"}); return;
            case OpKind::Xor: binary(w, {"xor rax, rbx"}); return;
            case OpKind::Not:
                comment(w);
                line(op.overload == 1 ? "xor qword [rsp], 1" : "not qword [rsp]");  // bool: logical; int: bitwise
                return;
            case OpKind::Shl: binary(w, {"mov rcx, rbx", "shl rax, cl"}); return;
            case OpKind::Shr: binary(w, {"mov rcx, rbx", "shr rax, cl"}); return;
            case OpKind::Sar: binary(w, {"mov rcx, rbx", "sar rax, cl"}); return;

            case OpKind::Load8: load(w, "movzx eax, byte [rax]"); return;
            case OpKind::Load16: load(w, "movzx eax, word [rax]"); return;
            case OpKind::Load32: load(w, "mov eax, dword [rax]"); return;
            case OpKind::Load64: load(w, "mov rax, qword [rax]"); return;
            case OpKind::Store8: store(w, "mov byte [rbx], al"); return;
            case OpKind::Store16: store(w, "mov word [rbx], ax"); return;
            case OpKind::Store32: store(w, "mov dword [rbx], eax"); return;
            case OpKind::Store64: store(w, "mov qword [rbx], rax"); return;

            case OpKind::Dup: simple(w, {"push qword [rsp]"}); return;
            case OpKind::Drop: simple(w, {"add rsp, 8"}); return;
            case OpKind::Swap: simple(w, {"pop rax", "pop rbx", "push rax", "push rbx"}); return;
            case OpKind::Over: simple(w, {"push qword [rsp + 8]"}); return;
            case OpKind::Rot: simple(w, {"pop rcx", "pop rbx", "pop rax", "push rbx", "push rcx", "push rax"}); return;
            case OpKind::Nip: simple(w, {"pop rax", "mov [rsp], rax"}); return;
            case OpKind::Tuck: simple(w, {"pop rbx", "pop rax", "push rbx", "push rax", "push rbx"}); return;

            case OpKind::InB: port_in(w, "in al, dx"); return;
            case OpKind::InW: port_in(w, "in ax, dx"); return;
            case OpKind::InD: port_in(w, "in eax, dx"); return;
            case OpKind::OutB: port_out(w, "out dx, al"); return;
            case OpKind::OutW: port_out(w, "out dx, ax"); return;
            case OpKind::OutD: port_out(w, "out dx, eax"); return;
            case OpKind::Cli: simple(w, {"cli"}); return;
            case OpKind::Sti: simple(w, {"sti"}); return;
            case OpKind::Hlt: simple(w, {"hlt"}); return;
            case OpKind::Lgdt: simple(w, {"pop rax", "lgdt [rax]"}); return;
            case OpKind::Lidt: simple(w, {"pop rax", "lidt [rax]"}); return;
            case OpKind::Invlpg: simple(w, {"pop rax", "invlpg [rax]"}); return;
            case OpKind::RdMsr: simple(w, {"pop rcx", "rdmsr", "shl rdx, 32", "or rax, rdx", "push rax"}); return;
            case OpKind::WrMsr: simple(w, {"pop rcx", "pop rax", "mov rdx, rax", "shr rdx, 32", "wrmsr"}); return;
            case OpKind::RdCr0: simple(w, {"mov rax, cr0", "push rax"}); return;
            case OpKind::RdCr2: simple(w, {"mov rax, cr2", "push rax"}); return;
            case OpKind::RdCr3: simple(w, {"mov rax, cr3", "push rax"}); return;
            case OpKind::RdCr4: simple(w, {"mov rax, cr4", "push rax"}); return;
            case OpKind::WrCr0: simple(w, {"pop rax", "mov cr0, rax"}); return;
            case OpKind::WrCr3: simple(w, {"pop rax", "mov cr3, rax"}); return;
            case OpKind::WrCr4: simple(w, {"pop rax", "mov cr4, rax"}); return;

            case OpKind::Call: emit_call(op); return;
            case OpKind::Execute: simple("execute", {"pop rax", "call rax"}); return;
            case OpKind::Asm: comment("invoke"); line(op.text); return;
            case OpKind::As:
                comment("as " + to_string(op.type));
                if (op.overload == 1) {  // normalise to 0 or 1
                    line("pop rax");
                    line("test rax, rax");
                    line("setne al");
                    line("movzx eax, al");
                    line("push rax");
                }
                return;

            case OpKind::If: {
                std::string else_lbl = new_label("else");
                std::string end_lbl = new_label("endif");
                comment("if");
                line("pop rax");
                line("test rax, rax");
                line("jz " + (op.has_else ? else_lbl : end_lbl));
                emit_block(op.body);
                if (op.has_else) {
                    line("jmp " + end_lbl);
                    label(else_lbl);
                    emit_block(op.else_body);
                }
                label(end_lbl);
                return;
            }
            case OpKind::While: {
                Loop loop{new_label("while"), new_label("endwhile")};
                label(loop.start);
                emit_block(op.cond);
                comment("do");
                line("pop rax");
                line("test rax, rax");
                line("jz " + loop.end);
                loops_.push_back(loop);
                emit_block(op.body);
                loops_.pop_back();
                line("jmp " + loop.start);
                label(loop.end);
                return;
            }
            case OpKind::Break: comment("break"); line("jmp " + loops_.back().end); return;
            case OpKind::Continue: comment("continue"); line("jmp " + loops_.back().start); return;
            case OpKind::Return: comment("return"); line("jmp " + ret_label(current_->name)); return;
            case OpKind::Let:
                comment("let");
                for (std::size_t i = op.slots.size(); i-- > 0;) line("pop " + frame_ref(op.slots[i]));
                emit_block(op.body);
                return;
            case OpKind::Set: comment("set " + op.text); line("pop " + frame_ref(op.value)); return;
        }
    }

    void emit_call(const Op& op) {
        if (prog_.externs.count(op.text)) {
            comment("call " + op.text + " (extern)");
            line(op.text);
            return;
        }
        const StackExprDef& def = prog_.stackexprs.at(op.text);
        if (def.kind != ExprKind::Inline) {
            line("call " + fn_label(def.name));
            return;
        }
        for (const StackExprDef* active : inlining_) {
            if (active == &def) {
                std::string chain;
                for (const StackExprDef* d : inlining_) chain += d->name + " -> ";
                throw CompileError(op.loc, "recursive inline stackexpr: " + chain + def.name +
                                               " (inline stackexprs are pasted in place; remove 'inline' to allow recursion)");
            }
        }
        comment("inline " + def.name);
        inlining_.push_back(&def);
        std::vector<Loop> saved = std::move(loops_);  // break/continue inside refer to its own loops
        loops_.clear();
        emit_block(def.body);
        loops_ = std::move(saved);
        inlining_.pop_back();
        comment("end inline " + def.name);
    }
};

}  // namespace

std::string generate(const Program& prog, const CodegenOptions& options) {
    return Generator(prog, options).run();
}

}  // namespace stackexpr

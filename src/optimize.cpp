#include "optimize.hpp"

#include <cctype>
#include <cstdint>
#include <optional>
#include <set>
#include <sstream>
#include <vector>

namespace stackexpr {

namespace {

constexpr const char* kIndent = "    ";

struct Insn {
    std::string mnemonic;
    std::string operands;  // without any trailing comment
};

bool is_comment_or_blank(const std::string& line) {
    for (char c : line) {
        if (c == ';') return true;
        if (!std::isspace(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

// An instruction line is indented by exactly four spaces and isn't a comment. Labels,
// directives and section names start in column 0.
std::optional<Insn> parse(const std::string& line) {
    if (line.compare(0, 4, kIndent) != 0 || line.size() <= 4 || std::isspace(static_cast<unsigned char>(line[4])) ||
        line[4] == ';') {
        return std::nullopt;
    }
    std::string body = line.substr(4);
    if (auto semi = body.find(';'); semi != std::string::npos) body.resize(semi);
    while (!body.empty() && std::isspace(static_cast<unsigned char>(body.back()))) body.pop_back();
    auto space = body.find(' ');
    Insn insn;
    insn.mnemonic = body.substr(0, space);
    if (space != std::string::npos) insn.operands = body.substr(space + 1);
    return insn;
}

// The 64-bit register a register name belongs to ("eax" -> "rax"), or "" if it isn't one.
std::string family(const std::string& name) {
    static const char* const groups[][5] = {
        {"rax", "eax", "ax", "al", "ah"},     {"rbx", "ebx", "bx", "bl", "bh"},
        {"rcx", "ecx", "cx", "cl", "ch"},     {"rdx", "edx", "dx", "dl", "dh"},
        {"rsi", "esi", "si", "sil", ""},      {"rdi", "edi", "di", "dil", ""},
        {"rbp", "ebp", "bp", "bpl", ""},      {"rsp", "esp", "sp", "spl", ""},
    };
    for (const auto& g : groups) {
        for (const char* n : g) {
            if (*n && name == n) return g[0];
        }
    }
    // r8..r15 and their r8d / r8w / r8b forms
    if (name.size() >= 2 && name[0] == 'r' && std::isdigit(static_cast<unsigned char>(name[1]))) {
        std::size_t i = 1;
        while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) ++i;
        std::string base = name.substr(0, i);
        std::string suffix = name.substr(i);
        int n = std::stoi(base.substr(1));
        if (n >= 8 && n <= 15 && (suffix.empty() || suffix == "d" || suffix == "w" || suffix == "b")) return base;
    }
    return {};
}

// Every register an operand string mentions, as 64-bit families.
std::set<std::string> registers_in(const std::string& text) {
    std::set<std::string> regs;
    std::string word;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        char c = i < text.size() ? text[i] : ' ';
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            word += c;
        } else {
            if (std::string f = family(word); !f.empty()) regs.insert(f);
            word.clear();
        }
    }
    return regs;
}

bool is_register(const std::string& operand) { return !family(operand).empty(); }

std::optional<std::int64_t> as_number(const std::string& s) {
    if (s.empty()) return std::nullopt;
    std::size_t i = s[0] == '-' ? 1 : 0;
    if (i == s.size()) return std::nullopt;
    for (std::size_t j = i; j < s.size(); ++j) {
        if (!std::isdigit(static_cast<unsigned char>(s[j]))) return std::nullopt;
    }
    try {
        return std::stoll(s);
    } catch (...) {
        return std::nullopt;
    }
}

bool fits_imm32(std::int64_t v) { return v >= INT32_MIN && v <= INT32_MAX; }

// `mov`/`movzx`/`lea` into a register: writes only that register, touches no memory
// and no stack. Returns the destination register's family.
std::optional<std::string> simple_register_write(const Insn& insn) {
    if (insn.mnemonic != "mov" && insn.mnemonic != "movzx" && insn.mnemonic != "lea") return std::nullopt;
    auto comma = insn.operands.find(',');
    if (comma == std::string::npos) return std::nullopt;
    std::string dest = insn.operands.substr(0, comma);
    std::string dest_family = family(dest);
    if (dest_family.empty() || registers_in(insn.operands).count("rsp")) return std::nullopt;
    return dest_family;
}

class Peephole {
public:
    explicit Peephole(const std::string& text) {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) lines_.push_back(line);
    }

    std::size_t instruction_count() const {
        std::size_t n = 0;
        for (const std::string& l : lines_) n += parse(l).has_value();
        return n;
    }

    void run() {
        while (pass()) {
        }
    }

    std::string text() const {
        std::string out;
        for (const std::string& l : lines_) out += l + "\n";
        return out;
    }

private:
    std::vector<std::string> lines_;

    // The next line after `i` that isn't a comment or blank, or lines_.size().
    std::size_t next_insn(std::size_t i) const {
        for (++i; i < lines_.size(); ++i) {
            if (!is_comment_or_blank(lines_[i])) return i;
        }
        return i;
    }

    void erase(std::size_t i) { lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(i)); }

    bool pass() {
        bool changed = false;
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            auto insn = parse(lines_[i]);
            if (!insn) continue;
            if (insn->mnemonic == "mov" && fold_constant_push(i, *insn)) {
                changed = true;
            } else if (insn->mnemonic.compare(0, 3, "set") == 0 && fuse_branch(i, *insn)) {
                changed = true;
            } else if (insn->mnemonic == "push" && remove_round_trip(i, insn->operands)) {
                changed = true;
                if (i > 0) --i;  // look at this position again
            }
        }
        return changed;
    }

    // setl al / movzx eax, al / test rax, rax / jz L  ->  jge L
    // A comparison whose bool is only used to branch jumps on the flags directly.
    bool fuse_branch(std::size_t i, const Insn& set) {
        static const std::pair<const char*, const char*> inverse[] = {
            {"sete", "jne"}, {"setne", "je"}, {"setl", "jge"}, {"setg", "jle"}, {"setle", "jg"},
            {"setge", "jl"}, {"setb", "jae"}, {"seta", "jbe"}, {"setbe", "ja"}, {"setae", "jb"},
        };
        const char* jump = nullptr;
        for (const auto& [s, j] : inverse) {
            if (set.mnemonic == s) jump = j;
        }
        if (!jump || set.operands != "al") return false;
        std::size_t a = next_insn(i), b = next_insn(a), c = next_insn(b);
        if (c >= lines_.size()) return false;
        auto movzx = parse(lines_[a]), test = parse(lines_[b]), jz = parse(lines_[c]);
        if (!movzx || movzx->mnemonic != "movzx" || movzx->operands != "eax, al") return false;
        if (!test || test->mnemonic != "test" || test->operands != "rax, rax") return false;
        if (!jz || jz->mnemonic != "jz") return false;
        lines_[c] = kIndent + std::string(jump) + " " + jz->operands;
        erase(b);
        erase(a);
        erase(i);
        return true;
    }

    // mov rax, 5 / push rax  ->  push 5
    bool fold_constant_push(std::size_t i, const Insn& mov) {
        auto comma = mov.operands.find(", ");
        if (comma == std::string::npos) return false;
        std::string reg = mov.operands.substr(0, comma);
        auto value = as_number(mov.operands.substr(comma + 2));
        if (!value || !fits_imm32(*value) || !is_register(reg)) return false;
        std::size_t j = next_insn(i);
        if (j >= lines_.size()) return false;
        auto push = parse(lines_[j]);
        if (!push || push->mnemonic != "push" || push->operands != reg) return false;
        lines_[i] = kIndent + std::string("push ") + std::to_string(*value);
        erase(j);
        return true;
    }

    // push X ... pop Y, with only simple register writes in between.
    //
    // X and Y may address memory relative to rsp: once both are gone, rsp at the mov is
    // what it was at the push (push reads its operand before moving rsp, and pop
    // addresses its operand after), so [rsp + n] still means the same slot.
    bool remove_round_trip(std::size_t i, const std::string& x) {
        if (x == "rsp") return false;
        std::set<std::string> x_regs = registers_in(x);
        bool x_memory = x.find('[') != std::string::npos;

        std::size_t j = next_insn(i);
        bool adjacent = true;
        while (j < lines_.size()) {
            auto insn = parse(lines_[j]);
            if (!insn) return false;  // label, directive: a barrier
            if (insn->mnemonic == "pop") break;
            if (insn->mnemonic == "add" && insn->operands == "rsp, 8" && adjacent) {
                erase(j);  // the pushed value is dropped straight away
                erase(i);
                return true;
            }
            auto written = simple_register_write(*insn);
            if (!written || x_regs.count(*written)) return false;
            adjacent = false;
            j = next_insn(j);
        }
        if (j >= lines_.size()) return false;

        std::string y = parse(lines_[j])->operands;
        bool y_memory = y.find('[') != std::string::npos;
        if (y == "rsp") return false;
        if (x_memory && y_memory) return false;  // no memory-to-memory mov
        if (y_memory && !is_register(x)) {
            auto value = as_number(x);
            if (!value || !fits_imm32(*value)) return false;
        }

        if (x == y) {
            erase(j);
        } else {
            lines_[j] = kIndent + std::string("mov ") + y + ", " + x;
        }
        erase(i);
        return true;
    }
};

}  // namespace

std::string optimize(const std::string& assembly, OptimizeStats* stats) {
    Peephole p(assembly);
    if (stats) stats->before = p.instruction_count();
    p.run();
    if (stats) stats->after = p.instruction_count();
    return p.text();
}

}  // namespace stackexpr

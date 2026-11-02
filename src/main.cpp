// stackexpr: compiler for the Stackexpr language.
//
//   stackexpr program.sxp            compile, assemble and link ./program
//   stackexpr program.sxp -o app     choose the output executable
//   stackexpr program.sxp -S         write program.asm only (no nasm/ld needed)

#include "codegen.hpp"
#include "lexer.hpp"
#include "parser.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void usage(std::ostream& os) {
    os << "usage: stackexpr <file> [-o <output>] [-S] [--keep-asm] [-L <lib-dir>]\n"
          "\n"
          "  -o <output>    output path (default: input name without extension)\n"
          "  -S             only generate assembly (<output>.asm)\n"
          "  --keep-asm     keep the generated .asm next to the executable\n"
          "  -L <lib-dir>   directory holding asm libraries for 'grab asm<...>'\n"
          "                 (default: $STACKEXPR_LIB, else <exe-dir>/../lib or <exe-dir>/lib)\n";
}

fs::path default_lib_dir() {
    if (const char* env = std::getenv("STACKEXPR_LIB")) return env;
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        for (const fs::path& candidate : {exe.parent_path().parent_path() / "lib", exe.parent_path() / "lib"}) {
            if (fs::is_directory(candidate)) return candidate;
        }
    }
    return "lib";
}

int run(const std::vector<std::string>& argv) {
    std::cerr << "+";
    for (const std::string& a : argv) std::cerr << ' ' << a;
    std::cerr << '\n';

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {
        std::vector<char*> args;
        for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);
        execvp(args[0], args.data());
        std::cerr << "stackexpr: could not run '" << argv[0] << "': " << std::strerror(errno) << '\n';
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string input, output, lib_arg;
    bool asm_only = false, keep_asm = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            usage(std::cout);
            return 0;
        } else if (a == "-o" && i + 1 < argc) {
            output = argv[++i];
        } else if (a == "-L" && i + 1 < argc) {
            lib_arg = argv[++i];
        } else if (a == "-S") {
            asm_only = true;
        } else if (a == "--keep-asm") {
            keep_asm = true;
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "stackexpr: unknown option '" << a << "'\n";
            usage(std::cerr);
            return 2;
        } else if (input.empty()) {
            input = a;
        } else {
            std::cerr << "stackexpr: only one input file is supported\n";
            return 2;
        }
    }
    if (input.empty()) {
        usage(std::cerr);
        return 2;
    }

    std::ifstream in(input, std::ios::binary);
    if (!in) {
        std::cerr << "stackexpr: cannot open '" << input << "'\n";
        return 1;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    const std::string source = buf.str();

    fs::path out_path = output.empty() ? fs::path(input).replace_extension() : fs::path(output);
    if (out_path == fs::path(input)) out_path += ".out";
    fs::path asm_path = out_path;
    asm_path += ".asm";
    fs::path lib_dir = lib_arg.empty() ? default_lib_dir() : fs::path(lib_arg);

    std::string assembly;
    try {
        std::vector<stackexpr::Token> tokens = stackexpr::lex(source);
        stackexpr::Program prog = stackexpr::parse(tokens);
        for (const stackexpr::Include& inc : prog.includes) {
            if (!fs::exists(lib_dir / (inc.lib + ".asm"))) {
                throw stackexpr::CompileError(inc.loc, "library asm<" + inc.lib + "> not found in " +
                                                           lib_dir.string());
            }
        }
        assembly = stackexpr::generate(prog);
    } catch (const stackexpr::CompileError& e) {
        std::cerr << input << ":" << e.loc.line << ":" << e.loc.column << ": error: " << e.what() << '\n';
        return 1;
    }

    {
        std::ofstream out(asm_path);
        if (!(out << assembly)) {
            std::cerr << "stackexpr: cannot write '" << asm_path.string() << "'\n";
            return 1;
        }
    }
    if (asm_only) return 0;

    fs::path obj_path = out_path;
    obj_path += ".o";
    std::string include_dir = lib_dir.string();
    if (include_dir.empty() || include_dir.back() != '/') include_dir += '/';

    int rc = run({"nasm", "-felf64", "-i", include_dir, asm_path.string(), "-o", obj_path.string()});
    if (rc == 0) rc = run({"ld", obj_path.string(), "-o", out_path.string()});

    std::error_code ec;
    fs::remove(obj_path, ec);
    if (!keep_asm && rc == 0) fs::remove(asm_path, ec);
    return rc;
}

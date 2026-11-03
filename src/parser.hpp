#pragma once

#include "ast.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace stackexpr {

// Loads the program starting at `path`, following every `grab`. `grab "file.sxp"` loads
// another source file relative to the grabbing one; `grab asm<name>` includes
// `<lib_dir>/name.asm`, plus `<lib_dir>/name.sxp` (its declarations) when that exists.
//
// `files` receives every loaded file (path and text); Loc::file indexes it. Throws
// CompileError on the first problem.
Program load_program(const std::string& path, const std::filesystem::path& lib_dir,
                     std::vector<SourceFile>& files);

// Built-in words (`dup`, `outb`, ...), which can't be used as names.
const std::map<std::string, OpKind>& builtin_words();

// True for names a definition or local may not use.
bool is_reserved_name(const std::string& name);

}  // namespace stackexpr

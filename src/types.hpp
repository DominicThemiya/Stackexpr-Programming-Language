#pragma once

#include <string>
#include <vector>

namespace stackexpr {

// The types a stack value can have. Every value is 64 bits at run time; types only
// exist at compile time.
enum class TypeKind {
    Int,   // a 64-bit integer
    Bool,  // the result of a comparison: 0 or 1
    Ptr,   // a memory address
    Var,   // a type variable such as `a` in `(a -- a a)`, standing for any type
};

struct Type {
    TypeKind kind = TypeKind::Int;
    std::string var;  // the variable's name, for TypeKind::Var

    static Type int_() { return {TypeKind::Int, {}}; }
    static Type bool_() { return {TypeKind::Bool, {}}; }
    static Type ptr() { return {TypeKind::Ptr, {}}; }
    static Type variable(std::string name) { return {TypeKind::Var, std::move(name)}; }

    bool operator==(const Type& o) const { return kind == o.kind && (kind != TypeKind::Var || var == o.var); }
    bool operator!=(const Type& o) const { return !(*this == o); }
};

inline std::string to_string(const Type& t) {
    switch (t.kind) {
        case TypeKind::Int: return "int";
        case TypeKind::Bool: return "bool";
        case TypeKind::Ptr: return "ptr";
        case TypeKind::Var: return t.var;
    }
    return "?";
}

// "int ptr", or "nothing" for an empty list.
inline std::string to_string(const std::vector<Type>& types) {
    if (types.empty()) return "nothing";
    std::string s;
    for (const Type& t : types) {
        if (!s.empty()) s += ' ';
        s += to_string(t);
    }
    return s;
}

// A stack effect: the types taken from the top of the stack and the types left in their place.
struct Signature {
    std::vector<Type> in;
    std::vector<Type> out;
};

// "(int ptr -- bool)", "( -- int)", "( -- )".
inline std::string to_string(const Signature& sig) {
    std::string s = "(";
    s += sig.in.empty() ? " " : to_string(sig.in) + " ";
    s += "--";
    s += sig.out.empty() ? " " : " " + to_string(sig.out);
    return s + ")";
}

}  // namespace stackexpr

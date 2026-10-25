// ==============================================================================
// NIX PROGRAMMING LANGUAGE - NATIVE C++ RUNTIME
// ------------------------------------------------------------------------------
// Header-only runtime shared by:
//   * the Nix interpreter   (`nix run file.nx`)
//   * the Nix compiler      (`nix build file.nx`) -> embedded in every binary
//
// It reproduces the semantics of the original Python implementation
// (Python-like dynamic values, truthiness, printing rules, modules
// <math>, <time>, <sys>, <random>, <builtins>) with native speed.
// ==============================================================================
#ifndef NIX_RUNTIME_HPP
#define NIX_RUNTIME_HPP

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#  include <io.h>
#  define NIX_POPEN _popen
#  define NIX_PCLOSE _pclose
#else
#  include <dirent.h>
#  include <sys/stat.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  define NIX_POPEN popen
#  define NIX_PCLOSE pclose
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define NIX_LIKELY(x) __builtin_expect(!!(x), 1)
#  define NIX_UNLIKELY(x) __builtin_expect(!!(x), 0)
#  define NIX_INLINE inline __attribute__((always_inline))
#  define NIX_NOINLINE __attribute__((noinline))
#else
#  define NIX_LIKELY(x) (x)
#  define NIX_UNLIKELY(x) (x)
#  define NIX_INLINE inline
#  define NIX_NOINLINE
#endif

namespace nix {

// ------------------------------------------------------------------------------
// Errors
// ------------------------------------------------------------------------------
struct RuntimeError : std::runtime_error {
    // internal == true mimics Python exceptions that were not NixRuntimeError
    // (TypeError, ZeroDivisionError...) -> "[Internal Interpreter Error]".
    bool internal;
    explicit RuntimeError(const std::string& m, bool internal_ = false)
        : std::runtime_error(m), internal(internal_) {}
};

[[noreturn]] NIX_NOINLINE inline void fail(const std::string& msg) { throw RuntimeError(msg); }
[[noreturn]] NIX_NOINLINE inline void fail_internal(const std::string& msg) { throw RuntimeError(msg, true); }

// ------------------------------------------------------------------------------
// Interned symbols (identifiers / field names)
// ------------------------------------------------------------------------------
struct Sym {
    std::string name;
    int id;
};

inline std::unordered_map<std::string, Sym*>& sym_table() {
    static std::unordered_map<std::string, Sym*> t;
    return t;
}
inline std::vector<Sym*>& sym_list() {
    static std::vector<Sym*> v;
    return v;
}
inline const Sym* intern(const std::string& s) {
    auto& t = sym_table();
    auto it = t.find(s);
    if (it != t.end()) return it->second;
    Sym* sym = new Sym{s, (int)sym_list().size()};
    sym_list().push_back(sym);
    t.emplace(s, sym);
    return sym;
}

// ------------------------------------------------------------------------------
// Values
// ------------------------------------------------------------------------------
enum class Tag : uint8_t { Undef, None, Bool, Int, Float, Str, List, Dict, Func, Class, Instance, Bound };

struct Obj {
    int32_t rc = 0;
    Tag kind;
    explicit Obj(Tag k) : kind(k) {}
    virtual ~Obj() = default;
};

struct Value {
    Tag tag;
    union {
        bool b;
        int64_t i;
        double d;
        Obj* o;
    };

    NIX_INLINE Value() : tag(Tag::Undef), i(0) {}
    NIX_INLINE static Value none() { Value v; v.tag = Tag::None; return v; }
    NIX_INLINE static Value boolean(bool x) { Value v; v.tag = Tag::Bool; v.i = 0; v.b = x; return v; }
    NIX_INLINE static Value integer(int64_t x) { Value v; v.tag = Tag::Int; v.i = x; return v; }
    NIX_INLINE static Value number(double x) { Value v; v.tag = Tag::Float; v.d = x; return v; }
    NIX_INLINE static Value object(Obj* p) { Value v; v.tag = p->kind; v.o = p; ++p->rc; return v; }

    NIX_INLINE bool heap() const { return tag >= Tag::Str; }
    NIX_INLINE Value(const Value& x) : tag(x.tag), i(x.i) { if (heap()) ++o->rc; }
    NIX_INLINE Value(Value&& x) noexcept : tag(x.tag), i(x.i) { x.tag = Tag::Undef; }
    NIX_INLINE Value& operator=(const Value& x) {
        if (x.heap()) ++x.o->rc;
        release();
        tag = x.tag; i = x.i;
        return *this;
    }
    NIX_INLINE Value& operator=(Value&& x) noexcept {
        if (this != &x) {
            release();
            tag = x.tag; i = x.i;
            x.tag = Tag::Undef;
        }
        return *this;
    }
    NIX_INLINE ~Value() { release(); }
    NIX_INLINE void release() {
        if (heap() && --o->rc == 0) delete o;
    }

    NIX_INLINE bool undef() const { return tag == Tag::Undef; }
    NIX_INLINE bool is_int_like() const { return tag == Tag::Int || tag == Tag::Bool; }
    NIX_INLINE bool is_num() const { return tag == Tag::Int || tag == Tag::Float || tag == Tag::Bool; }
    NIX_INLINE int64_t as_int() const { return tag == Tag::Bool ? (int64_t)b : i; }
    NIX_INLINE double as_double() const {
        return tag == Tag::Float ? d : (double)as_int();
    }
};

// ------------------------------------------------------------------------------
// Heap objects
// ------------------------------------------------------------------------------
struct StrObj : Obj {
    std::string s;
    explicit StrObj(std::string x) : Obj(Tag::Str), s(std::move(x)) {}
};
struct ListObj : Obj {
    std::vector<Value> items;
    ListObj() : Obj(Tag::List) {}
};

struct KeyHash { size_t operator()(const Value& v) const; };
struct KeyEq { bool operator()(const Value& a, const Value& b) const; };

struct DictObj : Obj {
    std::vector<std::pair<Value, Value>> entries;  // insertion ordered
    std::unordered_map<Value, size_t, KeyHash, KeyEq> index;
    DictObj() : Obj(Tag::Dict) {}
    Value* find(const Value& k) {
        auto it = index.find(k);
        return it == index.end() ? nullptr : &entries[it->second].second;
    }
    void set(const Value& k, const Value& v);
    bool erase(const Value& k, Value* out) {
        auto it = index.find(k);
        if (it == index.end()) return false;
        size_t pos = it->second;
        if (out) *out = entries[pos].second;
        entries.erase(entries.begin() + pos);
        index.clear();
        for (size_t j = 0; j < entries.size(); ++j) index.emplace(entries[j].first, j);
        return true;
    }
};

struct InstanceObj;
// Unified calling convention for every callable produced by the runtime,
// the compiler (generated C++ functions) and native modules.
using NativeFn = Value (*)(const Value* args, size_t n, const Value& self);

struct FuncObj : Obj {
    std::string name;
    int arity;          // -1 = variadic (native)
    NativeFn fn;        // compiled / native implementation (may be null)
    const void* node;   // interpreter AST node (may be null)
    bool is_method;     // defined inside a class body
    bool native;        // built-in from a module
    FuncObj(std::string n, int a, NativeFn f, const void* nd, bool m, bool nat)
        : Obj(Tag::Func), name(std::move(n)), arity(a), fn(f), node(nd), is_method(m), native(nat) {}
};

using ClassBodyFn = void (*)(const Value& self);
struct ClassObj : Obj {
    std::string name;
    ClassBodyFn body;   // compiled class body
    const void* node;   // interpreter ClassDef node
    ClassObj(std::string n, ClassBodyFn b, const void* nd) : Obj(Tag::Class), name(std::move(n)), body(b), node(nd) {}
};

struct InstanceObj : Obj {
    Value cls;
    std::vector<std::pair<const Sym*, Value>> fields;  // insertion ordered
    explicit InstanceObj(const Value& c) : Obj(Tag::Instance), cls(c) {}
    NIX_INLINE Value* find(const Sym* s) {
        for (auto& f : fields) if (f.first == s) return &f.second;
        return nullptr;
    }
    NIX_INLINE void set(const Sym* s, const Value& v) {
        if (Value* p = find(s)) *p = v; else fields.emplace_back(s, v);
    }
};

// Bound method: either a user method bound to an instance, or a built-in
// method (list.append, str.upper, ...) bound to a receiver.
struct BoundObj : Obj {
    Value self;
    Value func;        // FuncObj when user method
    const Sym* method; // builtin method name when func is undef
    BoundObj(Value s, Value f, const Sym* m) : Obj(Tag::Bound), self(std::move(s)), func(std::move(f)), method(m) {}
};

NIX_INLINE StrObj* as_str(const Value& v) { return static_cast<StrObj*>(v.o); }
NIX_INLINE ListObj* as_list(const Value& v) { return static_cast<ListObj*>(v.o); }
NIX_INLINE DictObj* as_dict(const Value& v) { return static_cast<DictObj*>(v.o); }
NIX_INLINE FuncObj* as_func(const Value& v) { return static_cast<FuncObj*>(v.o); }
NIX_INLINE ClassObj* as_class(const Value& v) { return static_cast<ClassObj*>(v.o); }
NIX_INLINE InstanceObj* as_inst(const Value& v) { return static_cast<InstanceObj*>(v.o); }
NIX_INLINE BoundObj* as_bound(const Value& v) { return static_cast<BoundObj*>(v.o); }

inline Value make_str(std::string s) { return Value::object(new StrObj(std::move(s))); }
inline Value make_list() { return Value::object(new ListObj()); }
inline Value make_list(std::vector<Value> items) {
    ListObj* l = new ListObj();
    l->items = std::move(items);
    return Value::object(l);
}
inline Value make_list(std::initializer_list<Value> items) {
    ListObj* l = new ListObj();
    l->items.assign(items.begin(), items.end());
    return Value::object(l);
}
inline Value make_dict() { return Value::object(new DictObj()); }
inline Value make_func(const std::string& name, int arity, NativeFn fn, const void* node, bool is_method, bool native) {
    return Value::object(new FuncObj(name, arity, fn, node, is_method, native));
}
inline Value make_class(const std::string& name, ClassBodyFn body, const void* node) {
    return Value::object(new ClassObj(name, body, node));
}

// ------------------------------------------------------------------------------
// Type names (Python style, used in error messages)
// ------------------------------------------------------------------------------
inline const char* type_name(const Value& v) {
    switch (v.tag) {
        case Tag::Undef: return "undefined";
        case Tag::None: return "NoneType";
        case Tag::Bool: return "bool";
        case Tag::Int: return "int";
        case Tag::Float: return "float";
        case Tag::Str: return "str";
        case Tag::List: return "list";
        case Tag::Dict: return "dict";
        case Tag::Func: return as_func(v)->native ? "builtin_function_or_method" : "FuncDefNode";
        case Tag::Class: return "ClassDefNode";
        case Tag::Instance: return "NixInstance";
        case Tag::Bound: return "method";
    }
    return "object";
}

// ------------------------------------------------------------------------------
// Number formatting (exactly like Python's str(float) / str(int))
// ------------------------------------------------------------------------------
inline std::string format_float(double x) {
    if (std::isnan(x)) return "nan";
    if (std::isinf(x)) return x > 0 ? "inf" : "-inf";
    if (x == 0.0) return std::signbit(x) ? "-0.0" : "0.0";
    char buf[64];
    int prec = 1;
    for (; prec <= 17; ++prec) {
        snprintf(buf, sizeof buf, "%.*e", prec - 1, x);
        if (strtod(buf, nullptr) == x) break;
    }
    // buf = "[-]d.ddde[+-]XX"
    std::string s(buf);
    bool neg = false;
    if (s[0] == '-') { neg = true; s.erase(0, 1); }
    size_t epos = s.find('e');
    int exp10 = atoi(s.c_str() + epos + 1);
    std::string digits;
    for (size_t k = 0; k < epos; ++k) if (s[k] != '.') digits += s[k];
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    std::string out;
    if (exp10 >= -4 && exp10 < 16) {  // Python repr() switches to exponent notation outside [1e-4, 1e16)
        if (exp10 < 0) {
            out = "0." + std::string(-exp10 - 1, '0') + digits;
        } else if ((int)digits.size() <= exp10 + 1) {
            out = digits + std::string(exp10 + 1 - digits.size(), '0') + ".0";
        } else {
            out = digits.substr(0, exp10 + 1) + "." + digits.substr(exp10 + 1);
        }
    }
    if (out.empty()) {
        out = digits.substr(0, 1);
        if (digits.size() > 1) out += "." + digits.substr(1);
        char eb[16];
        snprintf(eb, sizeof eb, "e%c%02d", exp10 < 0 ? '-' : '+', exp10 < 0 ? -exp10 : exp10);
        out += eb;
    }
    return neg ? "-" + out : out;
}

// Python's format_nix_value(): true/false lowercase, vectors and dicts
// printed recursively with str() semantics.
inline void display_into(std::string& out, const Value& v);
inline std::string to_display(const Value& v) {
    if (v.tag == Tag::Str) return as_str(v)->s;
    std::string s;
    display_into(s, v);
    return s;
}
inline void display_into(std::string& out, const Value& v) {
    switch (v.tag) {
        case Tag::Undef: out += "None"; break;
        case Tag::None: out += "None"; break;
        case Tag::Bool: out += v.b ? "true" : "false"; break;
        case Tag::Int: {
            char buf[32];
            snprintf(buf, sizeof buf, "%lld", (long long)v.i);
            out += buf;
            break;
        }
        case Tag::Float: out += format_float(v.d); break;
        case Tag::Str: out += as_str(v)->s; break;
        case Tag::List: {
            out += '[';
            bool first = true;
            for (auto& e : as_list(v)->items) {
                if (!first) out += ", ";
                first = false;
                display_into(out, e);
            }
            out += ']';
            break;
        }
        case Tag::Dict: {
            out += '{';
            bool first = true;
            for (auto& e : as_dict(v)->entries) {
                if (!first) out += ", ";
                first = false;
                display_into(out, e.first);
                out += ": ";
                display_into(out, e.second);
            }
            out += '}';
            break;
        }
        case Tag::Func:
            if (as_func(v)->native) out += "<built-in function " + as_func(v)->name + ">";
            else out += "<fct " + as_func(v)->name + ">";
            break;
        case Tag::Class: out += "<class " + as_class(v)->name + ">"; break;
        case Tag::Instance: out += "<" + as_class(as_inst(v)->cls)->name + " instance>"; break;
        case Tag::Bound:
            if (as_bound(v)->method) out += "<built-in method " + as_bound(v)->method->name + ">";
            else out += "<bound method " + as_func(as_bound(v)->func)->name + ">";
            break;
    }
}

// Python repr() of a value (used for error messages, e.g. KeyError)
inline std::string repr(const Value& v) {
    if (v.tag == Tag::Str) return "'" + as_str(v)->s + "'";
    return to_display(v);
}

// ------------------------------------------------------------------------------
// Truthiness & equality & hashing
// ------------------------------------------------------------------------------
NIX_INLINE bool truthy(const Value& v) {
    switch (v.tag) {
        case Tag::Bool: return v.b;
        case Tag::Int: return v.i != 0;
        case Tag::Float: return v.d != 0.0;
        case Tag::None: case Tag::Undef: return false;
        case Tag::Str: return !as_str(v)->s.empty();
        case Tag::List: return !as_list(v)->items.empty();
        case Tag::Dict: return !as_dict(v)->entries.empty();
        default: return true;
    }
}

inline bool equals(const Value& a, const Value& b) {
    if (a.is_num() && b.is_num()) {
        if (a.tag == Tag::Float || b.tag == Tag::Float) return a.as_double() == b.as_double();
        return a.as_int() == b.as_int();
    }
    if (a.tag != b.tag) return (a.tag == Tag::None || a.tag == Tag::Undef) && (b.tag == Tag::None || b.tag == Tag::Undef);
    switch (a.tag) {
        case Tag::None: case Tag::Undef: return true;
        case Tag::Str: return a.o == b.o || as_str(a)->s == as_str(b)->s;
        case Tag::List: {
            auto& x = as_list(a)->items; auto& y = as_list(b)->items;
            if (x.size() != y.size()) return false;
            for (size_t k = 0; k < x.size(); ++k) if (!equals(x[k], y[k])) return false;
            return true;
        }
        case Tag::Dict: {
            DictObj* x = as_dict(a); DictObj* y = as_dict(b);
            if (x->entries.size() != y->entries.size()) return false;
            for (auto& e : x->entries) {
                Value* o = y->find(e.first);
                if (!o || !equals(e.second, *o)) return false;
            }
            return true;
        }
        case Tag::Bound:
            return as_bound(a)->method == as_bound(b)->method && equals(as_bound(a)->self, as_bound(b)->self) &&
                   (as_bound(a)->func.o == as_bound(b)->func.o);
        default: return a.o == b.o;
    }
}

inline size_t KeyHash::operator()(const Value& v) const {
    switch (v.tag) {
        case Tag::Bool: case Tag::Int: return std::hash<int64_t>()(v.as_int());
        case Tag::Float: {
            double d = v.d;
            if (d == std::floor(d) && std::fabs(d) < 9.2e18) return std::hash<int64_t>()((int64_t)d);
            return std::hash<double>()(d);
        }
        case Tag::Str: return std::hash<std::string>()(as_str(v)->s);
        case Tag::None: return 0x9e3779b9;
        default: return std::hash<const void*>()(v.o);
    }
}
inline bool KeyEq::operator()(const Value& a, const Value& b) const { return equals(a, b); }

inline void check_hashable(const Value& k) {
    if (k.tag == Tag::List || k.tag == Tag::Dict)
        fail_internal(std::string("unhashable type: '") + type_name(k) + "'");
}
inline void DictObj::set(const Value& k, const Value& v) {
    auto it = index.find(k);
    if (it != index.end()) { entries[it->second].second = v; return; }
    check_hashable(k);
    index.emplace(k, entries.size());
    entries.emplace_back(k, v);
}

inline Value make_dict(std::initializer_list<Value> kv) {
    Value d = make_dict();
    DictObj* p = as_dict(d);
    for (auto it = kv.begin(); it != kv.end(); it += 2) {
        check_hashable(*it);
        p->set(*it, *(it + 1));
    }
    return d;
}

// ------------------------------------------------------------------------------
// Arithmetic (Python semantics: int/float/bool numeric tower, str & list ops)
// ------------------------------------------------------------------------------
[[noreturn]] NIX_NOINLINE inline void op_type_error(const char* op, const Value& a, const Value& b) {
    if (std::strcmp(op, "+") == 0 && a.tag == Tag::Str)
        fail_internal(std::string("can only concatenate str (not \"") + type_name(b) + "\") to str");
    if (std::strcmp(op, "+") == 0 && a.tag == Tag::List)
        fail_internal(std::string("can only concatenate list (not \"") + type_name(b) + "\") to list");
    fail_internal(std::string("unsupported operand type(s) for ") + op + ": '" + type_name(a) + "' and '" + type_name(b) + "'");
}

inline Value repeat_seq(const Value& seq, int64_t n) {
    if (seq.tag == Tag::Str) {
        std::string r;
        const std::string& s = as_str(seq)->s;
        if (n > 0) { r.reserve(s.size() * n); for (int64_t k = 0; k < n; ++k) r += s; }
        return make_str(std::move(r));
    }
    Value out = make_list();
    auto& src = as_list(seq)->items;
    auto& dst = as_list(out)->items;
    if (n > 0) { dst.reserve(src.size() * n); for (int64_t k = 0; k < n; ++k) dst.insert(dst.end(), src.begin(), src.end()); }
    return out;
}

NIX_NOINLINE inline Value add_slow(const Value& a, const Value& b) {
    if (a.is_num() && b.is_num()) {
        if (a.tag == Tag::Float || b.tag == Tag::Float) return Value::number(a.as_double() + b.as_double());
        int64_t r;
        if (__builtin_add_overflow(a.as_int(), b.as_int(), &r)) return Value::number((double)a.as_int() + (double)b.as_int());
        return Value::integer(r);
    }
    if (a.tag == Tag::Str || b.tag == Tag::Str) return make_str(to_display(a) + to_display(b));
    if (a.tag == Tag::List && b.tag == Tag::List) {
        Value out = make_list();
        auto& d = as_list(out)->items;
        d.reserve(as_list(a)->items.size() + as_list(b)->items.size());
        d.insert(d.end(), as_list(a)->items.begin(), as_list(a)->items.end());
        d.insert(d.end(), as_list(b)->items.begin(), as_list(b)->items.end());
        return out;
    }
    op_type_error("+", a, b);
}
NIX_INLINE Value add(const Value& a, const Value& b) {
    if (NIX_LIKELY(a.tag == Tag::Int && b.tag == Tag::Int)) {
        int64_t r;
        if (NIX_LIKELY(!__builtin_add_overflow(a.i, b.i, &r))) return Value::integer(r);
    } else if (a.tag == Tag::Float && b.tag == Tag::Float) {
        return Value::number(a.d + b.d);
    }
    return add_slow(a, b);
}

NIX_NOINLINE inline Value sub_slow(const Value& a, const Value& b) {
    if (a.is_num() && b.is_num()) {
        if (a.tag == Tag::Float || b.tag == Tag::Float) return Value::number(a.as_double() - b.as_double());
        int64_t r;
        if (__builtin_sub_overflow(a.as_int(), b.as_int(), &r)) return Value::number((double)a.as_int() - (double)b.as_int());
        return Value::integer(r);
    }
    op_type_error("-", a, b);
}
NIX_INLINE Value sub(const Value& a, const Value& b) {
    if (NIX_LIKELY(a.tag == Tag::Int && b.tag == Tag::Int)) {
        int64_t r;
        if (NIX_LIKELY(!__builtin_sub_overflow(a.i, b.i, &r))) return Value::integer(r);
    } else if (a.tag == Tag::Float && b.tag == Tag::Float) {
        return Value::number(a.d - b.d);
    }
    return sub_slow(a, b);
}

NIX_NOINLINE inline Value mul_slow(const Value& a, const Value& b) {
    if (a.is_num() && b.is_num()) {
        if (a.tag == Tag::Float || b.tag == Tag::Float) return Value::number(a.as_double() * b.as_double());
        int64_t r;
        if (__builtin_mul_overflow(a.as_int(), b.as_int(), &r)) return Value::number((double)a.as_int() * (double)b.as_int());
        return Value::integer(r);
    }
    if ((a.tag == Tag::Str || a.tag == Tag::List) && b.is_int_like()) return repeat_seq(a, b.as_int());
    if ((b.tag == Tag::Str || b.tag == Tag::List) && a.is_int_like()) return repeat_seq(b, a.as_int());
    if (a.tag == Tag::Str || a.tag == Tag::List || b.tag == Tag::Str || b.tag == Tag::List)
        fail_internal(std::string("can't multiply sequence by non-int of type '") +
                      type_name((a.tag == Tag::Str || a.tag == Tag::List) ? b : a) + "'");
    op_type_error("*", a, b);
}
NIX_INLINE Value mul(const Value& a, const Value& b) {
    if (NIX_LIKELY(a.tag == Tag::Int && b.tag == Tag::Int)) {
        int64_t r;
        if (NIX_LIKELY(!__builtin_mul_overflow(a.i, b.i, &r))) return Value::integer(r);
    } else if (a.tag == Tag::Float && b.tag == Tag::Float) {
        return Value::number(a.d * b.d);
    }
    return mul_slow(a, b);
}

inline Value divide(const Value& a, const Value& b) {
    // The original evaluator checks `right == 0` before dividing.
    if (b.is_num() && b.as_double() == 0.0) fail("Division by zero");
    if (a.is_num() && b.is_num()) return Value::number(a.as_double() / b.as_double());
    op_type_error("/", a, b);
}

inline Value mod(const Value& a, const Value& b) {
    if (a.is_int_like() && b.is_int_like()) {
        int64_t x = a.as_int(), y = b.as_int();
        if (y == 0) fail_internal("integer modulo by zero");
        if (y == -1) return Value::integer(0);
        int64_t r = x % y;
        if (r != 0 && ((r < 0) != (y < 0))) r += y;
        return Value::integer(r);
    }
    if (a.is_num() && b.is_num()) {
        double x = a.as_double(), y = b.as_double();
        if (y == 0.0) fail_internal("float modulo by zero");
        double r = std::fmod(x, y);
        if (r != 0.0) { if ((r < 0) != (y < 0)) r += y; }
        else r = std::copysign(0.0, y);
        return Value::number(r);
    }
    op_type_error("%", a, b);
}

inline Value neg(const Value& a) {
    if (a.tag == Tag::Float) return Value::number(-a.d);
    if (a.is_int_like()) {
        int64_t x = a.as_int();
        if (x == INT64_MIN) return Value::number(-(double)x);
        return Value::integer(-x);
    }
    fail("Unary minus operator requires a numeric type");
}

// Three-way ordering for <, >, <=, >= ; throws TypeError like Python.
inline int compare(const Value& a, const Value& b, const char* op) {
    if (a.is_num() && b.is_num()) {
        if (a.tag == Tag::Float || b.tag == Tag::Float) {
            double x = a.as_double(), y = b.as_double();
            return x < y ? -1 : (x > y ? 1 : 0);
        }
        int64_t x = a.as_int(), y = b.as_int();
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    if (a.tag == Tag::Str && b.tag == Tag::Str) {
        int c = as_str(a)->s.compare(as_str(b)->s);
        return c < 0 ? -1 : (c > 0 ? 1 : 0);
    }
    if (a.tag == Tag::List && b.tag == Tag::List) {
        auto& x = as_list(a)->items; auto& y = as_list(b)->items;
        size_t n = std::min(x.size(), y.size());
        for (size_t k = 0; k < n; ++k) {
            if (!equals(x[k], y[k])) return compare(x[k], y[k], op);
        }
        return x.size() < y.size() ? -1 : (x.size() > y.size() ? 1 : 0);
    }
    fail_internal(std::string("'") + op + "' not supported between instances of '" + type_name(a) + "' and '" + type_name(b) + "'");
}

NIX_INLINE bool lt_b(const Value& a, const Value& b) {
    if (NIX_LIKELY(a.tag == Tag::Int && b.tag == Tag::Int)) return a.i < b.i;
    if (a.is_num() && b.is_num()) return a.as_double() < b.as_double();
    return compare(a, b, "<") < 0;
}
NIX_INLINE bool gt_b(const Value& a, const Value& b) {
    if (NIX_LIKELY(a.tag == Tag::Int && b.tag == Tag::Int)) return a.i > b.i;
    if (a.is_num() && b.is_num()) return a.as_double() > b.as_double();
    return compare(a, b, ">") > 0;
}
NIX_INLINE bool le_b(const Value& a, const Value& b) {
    if (NIX_LIKELY(a.tag == Tag::Int && b.tag == Tag::Int)) return a.i <= b.i;
    if (a.is_num() && b.is_num()) return a.as_double() <= b.as_double();
    return compare(a, b, "<=") <= 0;
}
NIX_INLINE bool ge_b(const Value& a, const Value& b) {
    if (NIX_LIKELY(a.tag == Tag::Int && b.tag == Tag::Int)) return a.i >= b.i;
    if (a.is_num() && b.is_num()) return a.as_double() >= b.as_double();
    return compare(a, b, ">=") >= 0;
}
NIX_INLINE bool eq_b(const Value& a, const Value& b) {
    if (NIX_LIKELY(a.tag == Tag::Int && b.tag == Tag::Int)) return a.i == b.i;
    return equals(a, b);
}
NIX_INLINE bool ne_b(const Value& a, const Value& b) { return !eq_b(a, b); }
NIX_INLINE Value lt(const Value& a, const Value& b) { return Value::boolean(lt_b(a, b)); }
NIX_INLINE Value gt(const Value& a, const Value& b) { return Value::boolean(gt_b(a, b)); }
NIX_INLINE Value le(const Value& a, const Value& b) { return Value::boolean(le_b(a, b)); }
NIX_INLINE Value ge(const Value& a, const Value& b) { return Value::boolean(ge_b(a, b)); }
NIX_INLINE Value eq(const Value& a, const Value& b) { return Value::boolean(eq_b(a, b)); }
NIX_INLINE Value ne(const Value& a, const Value& b) { return Value::boolean(ne_b(a, b)); }

// ------------------------------------------------------------------------------
// Collections: subscripts
// ------------------------------------------------------------------------------
inline Value getitem(const Value& coll, const Value& idx) {
    if (coll.tag == Tag::List) {
        if (!idx.is_int_like())
            fail(std::string("Vector subscription index must be an integer, got '") + type_name(idx) + "'");
        auto& items = as_list(coll)->items;
        int64_t k = idx.as_int();
        int64_t n = (int64_t)items.size();
        int64_t real = k < 0 ? k + n : k;
        if (real < 0 || real >= n)
            fail("Vector index out of bounds: " + std::to_string(k) + " (vector size is " + std::to_string(n) + ")");
        return items[(size_t)real];
    }
    if (coll.tag == Tag::Dict) {
        Value* p = as_dict(coll)->find(idx);
        if (!p) {
            if (idx.tag == Tag::List || idx.tag == Tag::Dict) check_hashable(idx);
            fail("Key Error: '" + to_display(idx) + "' not found in dictionary");
        }
        return *p;
    }
    fail(std::string("Subscript lookup target must be a Vector or a Dictionary, got '") + type_name(coll) + "'");
}

inline void setitem(const Value& coll, const Value& idx, const Value& v) {
    if (coll.tag == Tag::Dict) { as_dict(coll)->set(idx, v); return; }
    if (coll.tag == Tag::List) {
        if (!idx.is_int_like())
            fail(std::string("Vector subscript index must be an integer, got '") + type_name(idx) + "'");
        auto& items = as_list(coll)->items;
        int64_t k = idx.as_int();
        int64_t n = (int64_t)items.size();
        int64_t real = k < 0 ? k + n : k;
        if (real < 0 || real >= n) fail("Vector index out of bounds: " + std::to_string(k));
        items[(size_t)real] = v;
        return;
    }
    fail(std::string("Subscript assignment target must be a Vector or Dictionary, got '") + type_name(coll) + "'");
}

// ------------------------------------------------------------------------------
// Calls
// ------------------------------------------------------------------------------
// Hook installed by the interpreter so that runtime helpers can call
// AST-based functions / classes.
using InterpCallFn = Value (*)(const Value& callee, const Value* args, size_t n, const Value& self);
inline InterpCallFn& interp_hook() {
    static InterpCallFn h = nullptr;
    return h;
}

inline Value call_builtin_method(const Value& recv, const Sym* name, const Value* args, size_t n);
inline Value instantiate(const Value& cls);

inline Value call_with_self(const Value& f, const Value* args, size_t n, const Value& self) {
    switch (f.tag) {
        case Tag::Func: {
            FuncObj* fo = as_func(f);
            if (fo->arity >= 0 && (size_t)fo->arity != n)
                fail("Function '" + fo->name + "' expects " + std::to_string(fo->arity) + " arguments, but received " +
                     std::to_string(n));
            if (fo->fn) return fo->fn(args, n, self);
            return interp_hook()(f, args, n, self);
        }
        case Tag::Class: return instantiate(f);
        case Tag::Bound: {
            BoundObj* bo = as_bound(f);
            if (bo->method) return call_builtin_method(bo->self, bo->method, args, n);
            return call_with_self(bo->func, args, n, bo->self);
        }
        default: break;
    }
    fail("'" + to_display(f) + "' is not callable");
}

NIX_INLINE Value call(const Value& f, const Value* args, size_t n) {
    static const Value no_self = Value::none();
    return call_with_self(f, args, n, no_self);
}
NIX_INLINE Value callv(const Value& f, std::initializer_list<Value> args) { return call(f, args.begin(), args.size()); }

inline Value instantiate(const Value& cls) {
    ClassObj* c = as_class(cls);
    Value inst = Value::object(new InstanceObj(cls));
    if (c->body) c->body(inst);
    else interp_hook()(cls, nullptr, 0, inst);
    return inst;
}

// Field read on an instance; user methods are bound to the instance.
NIX_INLINE Value bind_if_method(const Value& inst, const Value& v) {
    if (v.tag == Tag::Func && as_func(v)->is_method) return Value::object(new BoundObj(inst, v, nullptr));
    return v;
}

inline bool has_builtin_method(const Value& recv, const std::string& name);

inline Value getattr(const Value& obj, const Sym* name) {
    if (obj.tag == Tag::Instance) {
        Value* p = as_inst(obj)->find(name);
        if (!p) fail("Property '" + name->name + "' does not exist on this object");
        return bind_if_method(obj, *p);
    }
    if (has_builtin_method(obj, name->name)) return Value::object(new BoundObj(obj, Value(), name));
    fail("Cannot read property '" + name->name + "' on object of type '" + type_name(obj) + "'");
}

inline void setattr(const Value& obj, const Sym* name, const Value& v) {
    if (obj.tag != Tag::Instance) fail("Cannot assign property to a non-object");
    as_inst(obj)->set(name, v);
}

// obj.method(args): direct dispatch without allocating a bound object.
inline Value call_method(const Value& obj, const Sym* name, const Value* args, size_t n) {
    if (obj.tag == Tag::Instance) {
        Value* p = as_inst(obj)->find(name);
        if (!p) fail("Property '" + name->name + "' does not exist on this object");
        Value f = *p;
        if (f.tag == Tag::Func && as_func(f)->is_method) return call_with_self(f, args, n, obj);
        return call(f, args, n);
    }
    if (has_builtin_method(obj, name->name)) return call_builtin_method(obj, name, args, n);
    fail("Cannot read property '" + name->name + "' on object of type '" + type_name(obj) + "'");
}
NIX_INLINE Value call_method(const Value& obj, const Sym* name, std::initializer_list<Value> args) {
    return call_method(obj, name, args.begin(), args.size());
}

// Field access used by compiled class bodies / methods (bare identifiers).
inline Value field_get(const Value& self, const Sym* name) {
    if (self.tag == Tag::Instance) {
        if (Value* p = as_inst(self)->find(name)) return bind_if_method(self, *p);
    }
    fail("Undefined variable or symbol reference: '" + name->name + "'");
}
inline void field_set(const Value& self, const Sym* name, const Value& v) {
    if (self.tag != Tag::Instance) fail("Cannot assign to undefined variable: '" + name->name + "'");
    as_inst(self)->set(name, v);
}

[[noreturn]] NIX_NOINLINE inline void undefined_var(const std::string& name) {
    fail("Undefined variable or symbol reference: '" + name + "'");
}
NIX_INLINE const Value& rd(const Value& v, const char* name) {
    if (NIX_UNLIKELY(v.tag == Tag::Undef)) undefined_var(name);
    return v;
}

// ------------------------------------------------------------------------------
// Console I/O
// ------------------------------------------------------------------------------
inline void out(const Value* args, size_t n) {
    std::string line;
    for (size_t k = 0; k < n; ++k) {
        if (k) line += ' ';
        display_into(line, args[k]);
    }
    line += '\n';
    fwrite(line.data(), 1, line.size(), stdout);
}
NIX_INLINE void out(std::initializer_list<Value> args) { out(args.begin(), args.size()); }

inline bool py_isdigit(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char c : s) if (c < '0' || c > '9') return false;
    return true;
}
inline std::string strip_ws(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) ++a;
    while (b > a && isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
inline std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
// Python float() parsing (accepts surrounding whitespace, inf, nan, 1e5, _)
inline bool py_parse_float(const std::string& in, double& out) {
    std::string s = strip_ws(in);
    if (s.empty()) return false;
    std::string t;
    for (size_t k = 0; k < s.size(); ++k) {
        if (s[k] == '_') {
            if (k == 0 || k + 1 >= s.size() || !isdigit((unsigned char)s[k - 1]) || !isdigit((unsigned char)s[k + 1])) return false;
            continue;
        }
        t += s[k];
    }
    std::string l = lower(t);
    std::string body = (l[0] == '+' || l[0] == '-') ? l.substr(1) : l;
    if (body == "inf" || body == "infinity" || body == "nan") {
        double v = body == "nan" ? NAN : INFINITY;
        out = l[0] == '-' ? -v : v;
        return true;
    }
    for (char c : body) if (!(isdigit((unsigned char)c) || c == '.' || c == 'e' || c == '+' || c == '-')) return false;
    if (body.find('x') != std::string::npos) return false;
    char* end = nullptr;
    out = strtod(t.c_str(), &end);
    return end && *end == 0 && end != t.c_str();
}
inline bool py_parse_int(const std::string& in, int64_t& out) {
    std::string s = strip_ws(in);
    std::string t;
    for (char c : s) if (c != '_') t += c;
    if (t.empty()) return false;
    size_t k = (t[0] == '+' || t[0] == '-') ? 1 : 0;
    if (k >= t.size()) return false;
    for (size_t j = k; j < t.size(); ++j) if (!isdigit((unsigned char)t[j])) return false;
    errno = 0;
    out = strtoll(t.c_str(), nullptr, 10);
    return errno == 0;
}

inline Value auto_type_input(const std::string& user_input) {
    if (py_isdigit(user_input)) {
        int64_t v;
        if (py_parse_int(user_input, v)) return Value::integer(v);
        return Value::number(strtod(user_input.c_str(), nullptr));
    }
    double d;
    if (py_parse_float(user_input, d)) return Value::number(d);
    std::string l = lower(user_input);
    if (l == "true") return Value::boolean(true);
    if (l == "false") return Value::boolean(false);
    return make_str(user_input);
}

inline std::string read_line_raw(const std::string& prompt) {
    if (!prompt.empty()) fwrite(prompt.data(), 1, prompt.size(), stdout);
    fflush(stdout);
    std::string line;
    int c;
    bool any = false;
    while ((c = fgetc(stdin)) != EOF) {
        any = true;
        if (c == '\n') break;
        line += (char)c;
    }
    if (!any) fail_internal("EOF when reading a line");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

inline Value cin_read(const Value* prompt) {
    std::string p = prompt ? to_display(*prompt) : std::string();
    return auto_type_input(read_line_raw(p));
}

// ------------------------------------------------------------------------------
// Misc helpers used by generated code
// ------------------------------------------------------------------------------
inline int64_t loop_count(const Value& limit) {
    if (!limit.is_num()) fail("For loop limit must evaluate to a numeric value");
    if (limit.tag == Tag::Float) {
        if (std::isnan(limit.d)) fail_internal("cannot convert float NaN to integer");
        if (std::isinf(limit.d)) fail_internal("cannot convert float infinity to integer");
        return (int64_t)limit.d;
    }
    return limit.as_int();
}

inline std::vector<std::string>& program_args() {
    static std::vector<std::string> a;
    return a;
}

struct TopLevelReturn {
    Value value;
};

// ==============================================================================
// BUILT-IN METHODS on str / list / dict (Python methods reachable via `.`)
// ==============================================================================
inline void need_args(const char* name, size_t n, size_t lo, size_t hi) {
    if (n < lo || n > hi) {
        if (lo == hi)
            fail_internal(std::string(name) + "() takes exactly " + std::to_string(lo) + " argument(s) (" + std::to_string(n) + " given)");
        fail_internal(std::string(name) + "() takes from " + std::to_string(lo) + " to " + std::to_string(hi) + " arguments (" +
                      std::to_string(n) + " given)");
    }
}
inline const std::string& need_str(const Value& v, const char* ctx) {
    if (v.tag != Tag::Str) fail_internal(std::string(ctx) + ": expected str, got '" + type_name(v) + "'");
    return as_str(v)->s;
}

inline bool has_builtin_method(const Value& recv, const std::string& m) {
    static const char* str_m[] = {"upper", "lower", "strip", "lstrip", "rstrip", "split", "replace", "find", "startswith",
                                  "endswith", "join", "isdigit", "isalpha", "isalnum", "isspace", "count", "capitalize",
                                  "title", "index", "isupper", "islower", "splitlines", nullptr};
    static const char* list_m[] = {"append", "pop", "insert", "remove", "index", "count", "reverse", "sort", "clear",
                                   "extend", "copy", nullptr};
    static const char* dict_m[] = {"keys", "values", "items", "get", "pop", "update", "clear", "copy", "setdefault", nullptr};
    const char** table = recv.tag == Tag::Str ? str_m : recv.tag == Tag::List ? list_m : recv.tag == Tag::Dict ? dict_m : nullptr;
    if (!table) return false;
    for (; *table; ++table) if (m == *table) return true;
    return false;
}

inline std::vector<std::string> split_ws(const std::string& s, int64_t maxsplit) {
    std::vector<std::string> out;
    size_t k = 0, n = s.size();
    while (k < n) {
        while (k < n && isspace((unsigned char)s[k])) ++k;
        if (k >= n) break;
        if (maxsplit >= 0 && (int64_t)out.size() == maxsplit) {
            size_t e = n;
            while (e > k && isspace((unsigned char)s[e - 1])) --e;
            out.push_back(s.substr(k, e - k));
            return out;
        }
        size_t st = k;
        while (k < n && !isspace((unsigned char)s[k])) ++k;
        out.push_back(s.substr(st, k - st));
    }
    return out;
}

inline void sort_values(std::vector<Value>& v, bool reverse) {
    std::stable_sort(v.begin(), v.end(), [&](const Value& a, const Value& b) {
        return reverse ? lt_b(b, a) : lt_b(a, b);
    });
}

inline Value call_builtin_method(const Value& recv, const Sym* name, const Value* a, size_t n) {
    const std::string& m = name->name;
    if (recv.tag == Tag::Str) {
        const std::string& s = as_str(recv)->s;
        if (m == "upper" || m == "lower") {
            need_args(m.c_str(), n, 0, 0);
            std::string r = s;
            for (auto& c : r) c = (char)(m == "upper" ? toupper((unsigned char)c) : tolower((unsigned char)c));
            return make_str(r);
        }
        if (m == "strip" || m == "lstrip" || m == "rstrip") {
            need_args(m.c_str(), n, 0, 1);
            std::string chars = n ? need_str(a[0], m.c_str()) : std::string(" \t\n\r\v\f");
            size_t b = 0, e = s.size();
            if (m != "rstrip") while (b < e && chars.find(s[b]) != std::string::npos) ++b;
            if (m != "lstrip") while (e > b && chars.find(s[e - 1]) != std::string::npos) --e;
            return make_str(s.substr(b, e - b));
        }
        if (m == "split") {
            need_args("split", n, 0, 2);
            int64_t maxsplit = n >= 2 ? a[1].as_int() : -1;
            std::vector<Value> parts;
            if (n == 0 || a[0].tag == Tag::None) {
                for (auto& p : split_ws(s, maxsplit)) parts.push_back(make_str(p));
            } else {
                const std::string& sep = need_str(a[0], "split");
                if (sep.empty()) fail_internal("empty separator");
                size_t st = 0;
                int64_t cnt = 0;
                while (true) {
                    size_t pos = (maxsplit >= 0 && cnt >= maxsplit) ? std::string::npos : s.find(sep, st);
                    if (pos == std::string::npos) { parts.push_back(make_str(s.substr(st))); break; }
                    parts.push_back(make_str(s.substr(st, pos - st)));
                    st = pos + sep.size();
                    ++cnt;
                }
            }
            return make_list(std::move(parts));
        }
        if (m == "splitlines") {
            std::vector<Value> parts;
            size_t st = 0;
            for (size_t k = 0; k < s.size(); ++k) {
                if (s[k] == '\n' || s[k] == '\r') {
                    parts.push_back(make_str(s.substr(st, k - st)));
                    if (s[k] == '\r' && k + 1 < s.size() && s[k + 1] == '\n') ++k;
                    st = k + 1;
                }
            }
            if (st < s.size()) parts.push_back(make_str(s.substr(st)));
            return make_list(std::move(parts));
        }
        if (m == "replace") {
            need_args("replace", n, 2, 3);
            const std::string& from = need_str(a[0], "replace");
            const std::string& to = need_str(a[1], "replace");
            int64_t maxc = n == 3 ? a[2].as_int() : -1;
            std::string r;
            if (from.empty()) {
                int64_t cnt = 0;
                for (size_t k = 0; k <= s.size(); ++k) {
                    if (maxc < 0 || cnt < maxc) { r += to; ++cnt; }
                    if (k < s.size()) r += s[k];
                }
                return make_str(r);
            }
            size_t st = 0;
            int64_t cnt = 0;
            while (true) {
                size_t pos = (maxc >= 0 && cnt >= maxc) ? std::string::npos : s.find(from, st);
                if (pos == std::string::npos) { r += s.substr(st); break; }
                r += s.substr(st, pos - st);
                r += to;
                st = pos + from.size();
                ++cnt;
            }
            return make_str(r);
        }
        if (m == "find" || m == "index") {
            need_args(m.c_str(), n, 1, 1);
            size_t pos = s.find(need_str(a[0], m.c_str()));
            if (pos == std::string::npos) {
                if (m == "index") fail_internal("substring not found");
                return Value::integer(-1);
            }
            return Value::integer((int64_t)pos);
        }
        if (m == "count") {
            need_args("count", n, 1, 1);
            const std::string& sub = need_str(a[0], "count");
            if (sub.empty()) return Value::integer((int64_t)s.size() + 1);
            int64_t c = 0;
            for (size_t pos = s.find(sub); pos != std::string::npos; pos = s.find(sub, pos + sub.size())) ++c;
            return Value::integer(c);
        }
        if (m == "startswith") { need_args(m.c_str(), n, 1, 1); const std::string& p = need_str(a[0], "startswith"); return Value::boolean(s.compare(0, p.size(), p) == 0 && s.size() >= p.size()); }
        if (m == "endswith") { need_args(m.c_str(), n, 1, 1); const std::string& p = need_str(a[0], "endswith"); return Value::boolean(s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0); }
        if (m == "join") {
            need_args("join", n, 1, 1);
            std::string r;
            std::vector<Value> items;
            if (a[0].tag == Tag::List) items = as_list(a[0])->items;
            else if (a[0].tag == Tag::Dict) for (auto& e : as_dict(a[0])->entries) items.push_back(e.first);
            else if (a[0].tag == Tag::Str) for (char c : as_str(a[0])->s) items.push_back(make_str(std::string(1, c)));
            else fail_internal("can only join an iterable");
            for (size_t k = 0; k < items.size(); ++k) {
                if (items[k].tag != Tag::Str)
                    fail_internal("sequence item " + std::to_string(k) + ": expected str instance, " + type_name(items[k]) + " found");
                if (k) r += s;
                r += as_str(items[k])->s;
            }
            return make_str(r);
        }
        auto all_of = [&](int (*pred)(int)) {
            if (s.empty()) return Value::boolean(false);
            for (unsigned char c : s) if (!pred(c)) return Value::boolean(false);
            return Value::boolean(true);
        };
        if (m == "isdigit") return all_of(isdigit);
        if (m == "isalpha") return all_of(isalpha);
        if (m == "isalnum") return all_of(isalnum);
        if (m == "isspace") return all_of(isspace);
        if (m == "isupper" || m == "islower") {
            bool cased = false;
            for (unsigned char c : s) {
                if (isalpha(c)) { cased = true; if (m == "isupper" ? islower(c) : isupper(c)) return Value::boolean(false); }
            }
            return Value::boolean(cased);
        }
        if (m == "capitalize") {
            std::string r = lower(s);
            if (!r.empty()) r[0] = (char)toupper((unsigned char)r[0]);
            return make_str(r);
        }
        if (m == "title") {
            std::string r = s;
            bool prev = false;
            for (auto& c : r) {
                unsigned char u = (unsigned char)c;
                c = (char)(prev ? tolower(u) : toupper(u));
                prev = isalpha(u);
            }
            return make_str(r);
        }
    } else if (recv.tag == Tag::List) {
        auto& v = as_list(recv)->items;
        if (m == "append") { need_args("append", n, 1, 1); v.push_back(a[0]); return Value::none(); }
        if (m == "extend") {
            need_args("extend", n, 1, 1);
            if (a[0].tag == Tag::List) { std::vector<Value> tmp = as_list(a[0])->items; v.insert(v.end(), tmp.begin(), tmp.end()); }
            else if (a[0].tag == Tag::Str) for (char c : as_str(a[0])->s) v.push_back(make_str(std::string(1, c)));
            else if (a[0].tag == Tag::Dict) for (auto& e : as_dict(a[0])->entries) v.push_back(e.first);
            else fail_internal(std::string("'") + type_name(a[0]) + "' object is not iterable");
            return Value::none();
        }
        if (m == "pop") {
            need_args("pop", n, 0, 1);
            if (v.empty()) fail_internal("pop from empty list");
            int64_t k = n ? a[0].as_int() : -1;
            int64_t sz = (int64_t)v.size();
            if (k < 0) k += sz;
            if (k < 0 || k >= sz) fail_internal("pop index out of range");
            Value r = v[(size_t)k];
            v.erase(v.begin() + k);
            return r;
        }
        if (m == "insert") {
            need_args("insert", n, 2, 2);
            int64_t k = a[0].as_int(), sz = (int64_t)v.size();
            if (k < 0) k = std::max<int64_t>(0, k + sz);
            if (k > sz) k = sz;
            v.insert(v.begin() + k, a[1]);
            return Value::none();
        }
        if (m == "remove") {
            need_args("remove", n, 1, 1);
            for (size_t k = 0; k < v.size(); ++k) if (equals(v[k], a[0])) { v.erase(v.begin() + k); return Value::none(); }
            fail_internal("list.remove(x): x not in list");
        }
        if (m == "index") {
            need_args("index", n, 1, 1);
            for (size_t k = 0; k < v.size(); ++k) if (equals(v[k], a[0])) return Value::integer((int64_t)k);
            fail_internal(repr(a[0]) + " is not in list");
        }
        if (m == "count") {
            need_args("count", n, 1, 1);
            int64_t c = 0;
            for (auto& e : v) if (equals(e, a[0])) ++c;
            return Value::integer(c);
        }
        if (m == "reverse") { std::reverse(v.begin(), v.end()); return Value::none(); }
        if (m == "sort") { sort_values(v, n >= 1 && truthy(a[0])); return Value::none(); }
        if (m == "clear") { v.clear(); return Value::none(); }
        if (m == "copy") return make_list(std::vector<Value>(v));
    } else if (recv.tag == Tag::Dict) {
        DictObj* d = as_dict(recv);
        if (m == "keys") { std::vector<Value> r; for (auto& e : d->entries) r.push_back(e.first); return make_list(std::move(r)); }
        if (m == "values") { std::vector<Value> r; for (auto& e : d->entries) r.push_back(e.second); return make_list(std::move(r)); }
        if (m == "items") {
            std::vector<Value> r;
            for (auto& e : d->entries) r.push_back(make_list({e.first, e.second}));
            return make_list(std::move(r));
        }
        if (m == "get") {
            need_args("get", n, 1, 2);
            Value* p = d->find(a[0]);
            if (p) return *p;
            return n == 2 ? a[1] : Value::none();
        }
        if (m == "setdefault") {
            need_args("setdefault", n, 1, 2);
            Value* p = d->find(a[0]);
            if (p) return *p;
            Value def = n == 2 ? a[1] : Value::none();
            d->set(a[0], def);
            return def;
        }
        if (m == "pop") {
            need_args("pop", n, 1, 2);
            Value r;
            if (d->erase(a[0], &r)) return r;
            if (n == 2) return a[1];
            fail_internal(repr(a[0]));
        }
        if (m == "update") {
            need_args("update", n, 1, 1);
            if (a[0].tag != Tag::Dict) fail_internal("dict.update() expects a dictionary");
            auto entries = as_dict(a[0])->entries;
            for (auto& e : entries) d->set(e.first, e.second);
            return Value::none();
        }
        if (m == "clear") { d->entries.clear(); d->index.clear(); return Value::none(); }
        if (m == "copy") {
            Value r = make_dict();
            for (auto& e : d->entries) as_dict(r)->set(e.first, e.second);
            return r;
        }
    }
    fail("Cannot read property '" + m + "' on object of type '" + type_name(recv) + "'");
}

// ==============================================================================
// NATIVE MODULES: <math>, <time>, <sys>, <random>, <builtins>
// ==============================================================================
namespace qt_gui {
Value window_open(const Value* a, size_t n, const Value& self);
Value window_is_open(const Value* a, size_t n, const Value& self);
Value window_poll_events(const Value* a, size_t n, const Value& self);
Value window_clear(const Value* a, size_t n, const Value& self);
Value window_draw_rect(const Value* a, size_t n, const Value& self);
Value window_draw_circle(const Value* a, size_t n, const Value& self);
Value window_draw_line(const Value* a, size_t n, const Value& self);
Value window_draw_text(const Value* a, size_t n, const Value& self);
Value window_present(const Value* a, size_t n, const Value& self);
Value window_close(const Value* a, size_t n, const Value& self);
Value window_set_title(const Value* a, size_t n, const Value& self);

Value qt_app(const Value* a, size_t n, const Value& self);
Value qt_window(const Value* a, size_t n, const Value& self);
Value qt_show(const Value* a, size_t n, const Value& self);
Value qt_hide(const Value* a, size_t n, const Value& self);
Value qt_close(const Value* a, size_t n, const Value& self);
Value qt_run(const Value* a, size_t n, const Value& self);
Value qt_quit(const Value* a, size_t n, const Value& self);
Value qt_vbox(const Value* a, size_t n, const Value& self);
Value qt_hbox(const Value* a, size_t n, const Value& self);
Value qt_frame(const Value* a, size_t n, const Value& self);
Value qt_groupbox(const Value* a, size_t n, const Value& self);
Value qt_label(const Value* a, size_t n, const Value& self);
Value qt_button(const Value* a, size_t n, const Value& self);
Value qt_input(const Value* a, size_t n, const Value& self);
Value qt_textarea(const Value* a, size_t n, const Value& self);
Value qt_checkbox(const Value* a, size_t n, const Value& self);
Value qt_slider(const Value* a, size_t n, const Value& self);
Value qt_progressbar(const Value* a, size_t n, const Value& self);
Value qt_combobox(const Value* a, size_t n, const Value& self);
Value qt_listwidget(const Value* a, size_t n, const Value& self);
Value qt_set_text(const Value* a, size_t n, const Value& self);
Value qt_get_text(const Value* a, size_t n, const Value& self);
Value qt_set_value(const Value* a, size_t n, const Value& self);
Value qt_get_value(const Value* a, size_t n, const Value& self);
Value qt_is_checked(const Value* a, size_t n, const Value& self);
Value qt_set_checked(const Value* a, size_t n, const Value& self);
Value qt_set_style(const Value* a, size_t n, const Value& self);
Value qt_set_title(const Value* a, size_t n, const Value& self);
Value qt_resize(const Value* a, size_t n, const Value& self);
Value qt_set_enabled(const Value* a, size_t n, const Value& self);
Value qt_add_item(const Value* a, size_t n, const Value& self);
Value qt_clear_items(const Value* a, size_t n, const Value& self);
Value qt_on_click(const Value* a, size_t n, const Value& self);
Value qt_on_change(const Value* a, size_t n, const Value& self);
Value qt_timer(const Value* a, size_t n, const Value& self);
Value qt_msgbox(const Value* a, size_t n, const Value& self);
Value qt_confirm(const Value* a, size_t n, const Value& self);
Value qt_file_dialog(const Value* a, size_t n, const Value& self);
Value qt_color_dialog(const Value* a, size_t n, const Value& self);
void auto_exec_if_windows_open();
}  // namespace qt_gui

namespace natives {

inline void need(const char* msg, size_t n, size_t k) { if (n < k) fail(msg); }
inline double num(const Value& v, const char* fn) {
    if (!v.is_num()) fail_internal(std::string(fn) + ": must be real number, not " + type_name(v));
    return v.as_double();
}

static const char* const PI_100_STR =
    "3.1415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679";

// ---- math -------------------------------------------------------------------
inline Value math_get_pi(const Value*, size_t, const Value&) { return Value::number(3.141592653589793); }
inline Value hp_mul(const Value* a, size_t n, const Value&) {
    need("hp_mul expects 2 arguments: (val1, val2)", n, 2);
    return Value::number(num(a[0], "hp_mul") * num(a[1], "hp_mul"));
}
inline Value hp_div(const Value* a, size_t n, const Value&) {
    need("hp_div expects 2 arguments: (val1, val2)", n, 2);
    double d = num(a[1], "hp_div");
    if (d == 0) fail("High-precision division failed: Nix Runtime Error: High-precision division by zero");
    return Value::number(num(a[0], "hp_div") / d);
}
inline Value math_abs(const Value* a, size_t n, const Value&) {
    need("math_abs expects 1 argument: (x)", n, 1);
    if (a[0].tag == Tag::Float) return Value::number(std::fabs(a[0].d));
    if (a[0].is_int_like()) { int64_t x = a[0].as_int(); return x < 0 ? neg(a[0]) : Value::integer(x); }
    fail("Native execution error: bad operand type for abs(): '" + std::string(type_name(a[0])) + "'");
}
inline Value math_sqrt(const Value* a, size_t n, const Value&) {
    need("math_sqrt expects 1 argument: (x)", n, 1);
    double x = num(a[0], "math_sqrt");
    if (x < 0) fail("Cannot calculate the square root of a negative number");
    return Value::number(std::sqrt(x));
}
inline Value to_int_value(double r) {
    if (std::isnan(r) || std::isinf(r)) fail_internal("cannot convert float to integer");
    if (std::fabs(r) < 9.2e18) return Value::integer((int64_t)r);
    return Value::number(r);
}
inline Value math_ceil(const Value* a, size_t n, const Value&) {
    need("math_ceil expects 1 argument: (x)", n, 1);
    if (a[0].is_int_like()) return Value::integer(a[0].as_int());
    return to_int_value(std::ceil(num(a[0], "math_ceil")));
}
inline Value math_floor(const Value* a, size_t n, const Value&) {
    need("math_floor expects 1 argument: (x)", n, 1);
    if (a[0].is_int_like()) return Value::integer(a[0].as_int());
    return to_int_value(std::floor(num(a[0], "math_floor")));
}
inline Value math_pow(const Value* a, size_t n, const Value&) {
    need("math_pow expects 2 arguments: (base, exponent)", n, 2);
    return Value::number(std::pow(num(a[0], "math_pow"), num(a[1], "math_pow")));
}
inline Value math_sin(const Value* a, size_t n, const Value&) { need("math_sin expects 1 argument: (x_radians)", n, 1); return Value::number(std::sin(num(a[0], "math_sin"))); }
inline Value math_cos(const Value* a, size_t n, const Value&) { need("math_cos expects 1 argument: (x_radians)", n, 1); return Value::number(std::cos(num(a[0], "math_cos"))); }
inline Value math_tan(const Value* a, size_t n, const Value&) { need("math_tan expects 1 argument: (x_radians)", n, 1); return Value::number(std::tan(num(a[0], "math_tan"))); }
inline Value math_log(const Value* a, size_t n, const Value&) {
    need("math_log expects at least 1 argument: (x, [base])", n, 1);
    double x = num(a[0], "math_log");
    if (x <= 0) fail("Logarithm argument must be greater than zero");
    if (n >= 2) return Value::number(std::log(x) / std::log(num(a[1], "math_log")));
    return Value::number(std::log(x));
}
inline Value math_factorial(const Value* a, size_t n, const Value&) {
    need("math_factorial expects 1 argument: (n)", n, 1);
    if (!a[0].is_int_like() || a[0].as_int() < 0) fail("Factorial requires a non-negative integer");
    int64_t k = a[0].as_int();
    Value r = Value::integer(1);
    for (int64_t j = 2; j <= k; ++j) r = mul(r, Value::integer(j));
    return r;
}

// ---- time -------------------------------------------------------------------
inline Value time_sleep(const Value* a, size_t n, const Value&) {
    need("time_sleep requiert 1 argument : (secondes)", n, 1);
    if (!a[0].is_num()) fail("La durée passée à time_sleep doit être un nombre valide.");
    double d = a[0].as_double();
    if (d < 0) fail("La durée de pause ne peut pas être négative.");
    fflush(stdout);
    std::this_thread::sleep_for(std::chrono::duration<double>(d));
    return Value::none();
}
inline Value time_now(const Value*, size_t, const Value&) {
    using namespace std::chrono;
    return Value::number(duration<double>(system_clock::now().time_since_epoch()).count());
}
inline Value time_format(const Value* a, size_t n, const Value&) {
    double t = n > 0 ? num(a[0], "time_format") : time_now(nullptr, 0, Value()).d;
    std::string fmt = n > 1 ? to_display(a[1]) : "%Y-%m-%d %H:%M:%S";
    time_t tt = (time_t)t;
    struct tm lt;
#if defined(_WIN32)
    localtime_s(&lt, &tt);
#else
    localtime_r(&tt, &lt);
#endif
    char buf[1024];
    size_t len = strftime(buf, sizeof buf, fmt.c_str(), &lt);
    return make_str(std::string(buf, len));
}

// ---- sys --------------------------------------------------------------------
inline bool path_exists(const std::string& p) {
#if defined(_WIN32)
    return _access(p.c_str(), 0) == 0;
#else
    struct stat st;
    return stat(p.c_str(), &st) == 0;
#endif
}
inline Value file_write(const Value* a, size_t n, const Value&) {
    need("file_write requires 2 arguments: (file_path, content)", n, 2);
    std::string path = to_display(a[0]), content = to_display(a[1]);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) fail("Failed to write to file '" + path + "': " + strerror(errno));
    fwrite(content.data(), 1, content.size(), f);
    fclose(f);
    return Value::boolean(true);
}
inline std::string read_file_or_fail(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) fail("Failed to read file '" + path + "': " + strerror(errno));
    std::string data;
    char buf[65536];
    size_t got;
    while ((got = fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, got);
    fclose(f);
    return data;
}
inline Value file_read(const Value* a, size_t n, const Value&) {
    need("file_read requires 1 argument: (file_path)", n, 1);
    std::string path = to_display(a[0]);
    if (!path_exists(path)) fail("File '" + path + "' does not exist.");
    std::string data = read_file_or_fail(path);
    // Python text mode: universal newlines
    std::string r;
    r.reserve(data.size());
    for (size_t k = 0; k < data.size(); ++k) {
        if (data[k] == '\r') { r += '\n'; if (k + 1 < data.size() && data[k + 1] == '\n') ++k; }
        else r += data[k];
    }
    return make_str(r);
}
inline Value file_exists(const Value* a, size_t n, const Value&) {
    need("file_exists requires 1 argument: (path)", n, 1);
    return Value::boolean(path_exists(to_display(a[0])));
}
inline Value file_delete(const Value* a, size_t n, const Value&) {
    need("file_delete requires 1 argument: (file_path)", n, 1);
    std::string path = to_display(a[0]);
    if (!path_exists(path)) return Value::boolean(false);
    if (std::remove(path.c_str()) != 0) fail("Failed to delete file '" + path + "': " + strerror(errno));
    return Value::boolean(true);
}
inline Value sys_args(const Value*, size_t, const Value&) {
    std::vector<Value> r;
    for (auto& s : program_args()) r.push_back(make_str(s));
    return make_list(std::move(r));
}
inline Value sys_execute(const Value* a, size_t n, const Value&) {
    need("sys_execute requires 1 argument: (command)", n, 1);
    std::string cmd = to_display(a[0]);
    fflush(stdout);
#if defined(_WIN32)
    std::string full = cmd;
    std::string errfile;
#else
    char tmpl[] = "/tmp/nix_stderr_XXXXXX";
    int fd = mkstemp(tmpl);
    std::string errfile = fd >= 0 ? tmpl : "";
    if (fd >= 0) close(fd);
    std::string full = "( " + cmd + " ) 2>" + (errfile.empty() ? std::string("/dev/null") : "'" + errfile + "'");
#endif
    FILE* p = NIX_POPEN(full.c_str(), "r");
    if (!p) fail("Error executing command '" + cmd + "': " + strerror(errno));
    std::string outs;
    char buf[4096];
    size_t got;
    while ((got = fread(buf, 1, sizeof buf, p)) > 0) outs.append(buf, got);
    int status = NIX_PCLOSE(p);
    int code = status;
#if !defined(_WIN32)
    if (WIFEXITED(status)) code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) code = -WTERMSIG(status);
#endif
    std::string errs;
    if (!errfile.empty()) {
        FILE* f = fopen(errfile.c_str(), "rb");
        if (f) { while ((got = fread(buf, 1, sizeof buf, f)) > 0) errs.append(buf, got); fclose(f); }
        std::remove(errfile.c_str());
    }
    return make_dict({make_str("stdout"), make_str(outs), make_str("stderr"), make_str(errs), make_str("code"),
                      Value::integer(code)});
}
inline Value os_name(const Value*, size_t, const Value&) {
#if defined(_WIN32)
    return make_str("nt");
#else
    return make_str("posix");
#endif
}
inline Value os_listdir(const Value* a, size_t n, const Value&) {
    std::string path = n > 0 ? to_display(a[0]) : ".";
    std::vector<Value> r;
#if !defined(_WIN32)
    DIR* d = opendir(path.c_str());
    if (!d) fail("Failed to list directory '" + path + "': " + strerror(errno));
    while (struct dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        r.push_back(make_str(name));
    }
    closedir(d);
#endif
    return make_list(std::move(r));
}

// ---- random -----------------------------------------------------------------
inline std::mt19937_64& rng() {
    static std::mt19937_64 g((uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return g;
}
inline Value rnd_random(const Value*, size_t, const Value&) {
    return Value::number(std::uniform_real_distribution<double>(0.0, 1.0)(rng()));
}
inline Value rnd_seed(const Value* a, size_t n, const Value&) {
    rng().seed(n ? (uint64_t)KeyHash()(a[0]) : 0);
    return Value::none();
}
inline Value rnd_randint(const Value* a, size_t n, const Value&) {
    if (n != 2) fail_internal("randint() takes 2 arguments");
    int64_t lo = a[0].as_int(), hi = a[1].as_int();
    if (lo > hi) fail_internal("empty range for randrange()");
    return Value::integer(std::uniform_int_distribution<int64_t>(lo, hi)(rng()));
}
inline Value rnd_randrange(const Value* a, size_t n, const Value&) {
    if (n < 1 || n > 3) fail_internal("randrange() takes 1 to 3 arguments");
    int64_t start = n == 1 ? 0 : a[0].as_int();
    int64_t stop = n == 1 ? a[0].as_int() : a[1].as_int();
    int64_t step = n == 3 ? a[2].as_int() : 1;
    if (step == 0) fail_internal("zero step for randrange()");
    int64_t count = step > 0 ? (stop - start + step - 1) / step : (start - stop - step - 1) / (-step);
    if (count <= 0) fail_internal("empty range for randrange()");
    return Value::integer(start + step * std::uniform_int_distribution<int64_t>(0, count - 1)(rng()));
}
inline Value rnd_uniform(const Value* a, size_t n, const Value&) {
    if (n != 2) fail_internal("uniform() takes 2 arguments");
    double lo = num(a[0], "uniform"), hi = num(a[1], "uniform");
    return Value::number(lo + (hi - lo) * std::uniform_real_distribution<double>(0.0, 1.0)(rng()));
}
inline Value rnd_choice(const Value* a, size_t n, const Value&) {
    if (n != 1) fail_internal("choice() takes 1 argument");
    if (a[0].tag == Tag::List) {
        auto& v = as_list(a[0])->items;
        if (v.empty()) fail_internal("Cannot choose from an empty sequence");
        return v[std::uniform_int_distribution<size_t>(0, v.size() - 1)(rng())];
    }
    if (a[0].tag == Tag::Str) {
        auto& s = as_str(a[0])->s;
        if (s.empty()) fail_internal("Cannot choose from an empty sequence");
        return make_str(std::string(1, s[std::uniform_int_distribution<size_t>(0, s.size() - 1)(rng())]));
    }
    fail_internal("choice() requires a sequence");
}
inline Value rnd_shuffle(const Value* a, size_t n, const Value&) {
    if (n != 1 || a[0].tag != Tag::List) fail_internal("shuffle() requires a list");
    std::shuffle(as_list(a[0])->items.begin(), as_list(a[0])->items.end(), rng());
    return Value::none();
}

// ---- builtins (Python's builtins module, reachable with `using <builtins>`) --
inline Value b_len(const Value* a, size_t n, const Value&) {
    if (n != 1) fail_internal("len() takes exactly one argument");
    switch (a[0].tag) {
        case Tag::Str: return Value::integer((int64_t)as_str(a[0])->s.size());
        case Tag::List: return Value::integer((int64_t)as_list(a[0])->items.size());
        case Tag::Dict: return Value::integer((int64_t)as_dict(a[0])->entries.size());
        default: fail_internal(std::string("object of type '") + type_name(a[0]) + "' has no len()");
    }
}
inline Value b_str(const Value* a, size_t n, const Value&) {
    if (n == 0) return make_str("");
    const Value& v = a[0];
    if (v.tag == Tag::Bool) return make_str(v.b ? "True" : "False");
    return make_str(to_display(v));
}
inline Value b_int(const Value* a, size_t n, const Value&) {
    if (n == 0) return Value::integer(0);
    const Value& v = a[0];
    if (v.is_int_like()) return Value::integer(v.as_int());
    if (v.tag == Tag::Float) return to_int_value(std::trunc(v.d));
    if (v.tag == Tag::Str) {
        int64_t r;
        if (py_parse_int(as_str(v)->s, r)) return Value::integer(r);
        fail_internal("invalid literal for int() with base 10: '" + as_str(v)->s + "'");
    }
    fail_internal(std::string("int() argument must be a string or a number, not '") + type_name(v) + "'");
}
inline Value b_float(const Value* a, size_t n, const Value&) {
    if (n == 0) return Value::number(0.0);
    const Value& v = a[0];
    if (v.is_num()) return Value::number(v.as_double());
    if (v.tag == Tag::Str) {
        double d;
        if (py_parse_float(as_str(v)->s, d)) return Value::number(d);
        fail_internal("could not convert string to float: '" + as_str(v)->s + "'");
    }
    fail_internal(std::string("float() argument must be a string or a real number, not '") + type_name(v) + "'");
}
inline Value b_bool(const Value* a, size_t n, const Value&) { return Value::boolean(n && truthy(a[0])); }
inline Value b_list(const Value* a, size_t n, const Value&) {
    if (n == 0) return make_list();
    Value r = make_list();
    Value self = r;
    Value tmp[1] = {a[0]};
    static const Sym* ext = intern("extend");
    call_builtin_method(r, ext, tmp, 1);
    return r;
}
inline std::vector<Value> iter_values(const Value& v) {
    if (v.tag == Tag::List) return as_list(v)->items;
    if (v.tag == Tag::Dict) { std::vector<Value> r; for (auto& e : as_dict(v)->entries) r.push_back(e.first); return r; }
    if (v.tag == Tag::Str) { std::vector<Value> r; for (char c : as_str(v)->s) r.push_back(make_str(std::string(1, c))); return r; }
    fail_internal(std::string("'") + type_name(v) + "' object is not iterable");
}
inline Value minmax(const Value* a, size_t n, bool want_max) {
    std::vector<Value> items = n == 1 ? iter_values(a[0]) : std::vector<Value>(a, a + n);
    if (items.empty()) fail_internal(std::string(want_max ? "max" : "min") + "() arg is an empty sequence");
    Value best = items[0];
    for (size_t k = 1; k < items.size(); ++k)
        if (want_max ? gt_b(items[k], best) : lt_b(items[k], best)) best = items[k];
    return best;
}
inline Value b_min(const Value* a, size_t n, const Value&) { if (!n) fail_internal("min expected at least 1 argument"); return minmax(a, n, false); }
inline Value b_max(const Value* a, size_t n, const Value&) { if (!n) fail_internal("max expected at least 1 argument"); return minmax(a, n, true); }
inline Value b_sum(const Value* a, size_t n, const Value&) {
    if (n < 1) fail_internal("sum() takes at least 1 positional argument");
    Value acc = n > 1 ? a[1] : Value::integer(0);
    for (auto& v : iter_values(a[0])) acc = add(acc, v);
    return acc;
}
inline Value b_range(const Value* a, size_t n, const Value&) {
    if (n < 1 || n > 3) fail_internal("range expected 1 to 3 arguments");
    int64_t start = n == 1 ? 0 : a[0].as_int();
    int64_t stop = n == 1 ? a[0].as_int() : a[1].as_int();
    int64_t step = n == 3 ? a[2].as_int() : 1;
    if (step == 0) fail_internal("range() arg 3 must not be zero");
    std::vector<Value> r;
    for (int64_t k = start; step > 0 ? k < stop : k > stop; k += step) r.push_back(Value::integer(k));
    return make_list(std::move(r));
}
inline Value b_sorted(const Value* a, size_t n, const Value&) {
    if (n < 1) fail_internal("sorted expected 1 argument");
    std::vector<Value> items = iter_values(a[0]);
    sort_values(items, n >= 2 && truthy(a[1]));
    return make_list(std::move(items));
}
inline Value b_round(const Value* a, size_t n, const Value&) {
    if (n < 1) fail_internal("round() missing required argument 'number'");
    double x = num(a[0], "round");
    if (n == 1 || a[1].tag == Tag::None) {
        if (a[0].is_int_like()) return Value::integer(a[0].as_int());
        return to_int_value(std::nearbyint(x));  // banker's rounding (FE_TONEAREST)
    }
    int64_t nd = a[1].as_int();
    double p = std::pow(10.0, (double)nd);
    double r = std::nearbyint(x * p) / p;
    if (a[0].is_int_like()) return Value::integer((int64_t)r);
    return Value::number(r);
}
inline Value b_abs(const Value* a, size_t n, const Value& s) { return math_abs(a, n, s); }
inline Value b_print(const Value* a, size_t n, const Value&) {
    std::string line;
    for (size_t k = 0; k < n; ++k) {
        if (k) line += ' ';
        if (a[k].tag == Tag::Bool) line += a[k].b ? "True" : "False";
        else display_into(line, a[k]);
    }
    line += '\n';
    fwrite(line.data(), 1, line.size(), stdout);
    return Value::none();
}
inline Value b_input(const Value* a, size_t n, const Value&) {
    return make_str(read_line_raw(n ? to_display(a[0]) : std::string()));
}
inline Value b_type(const Value* a, size_t n, const Value&) {
    if (n != 1) fail_internal("type() takes 1 argument");
    return make_str(std::string("<class '") + type_name(a[0]) + "'>");
}

struct NativeDef {
    const char* name;
    NativeFn fn;
};
struct ConstDef {
    const char* name;
    Value (*make)();
};
struct ModuleDef {
    const char* name;
    std::vector<NativeDef> functions;
    std::vector<ConstDef> constants;
};

inline const std::vector<ModuleDef>& modules() {
    static const std::vector<ModuleDef> mods = {
        {"math",
         {{"math_get_pi", math_get_pi}, {"hp_mul", hp_mul}, {"hp_div", hp_div}, {"math_abs", math_abs},
          {"math_sqrt", math_sqrt}, {"math_ceil", math_ceil}, {"math_floor", math_floor}, {"math_pow", math_pow},
          {"math_sin", math_sin}, {"math_cos", math_cos}, {"math_tan", math_tan}, {"math_log", math_log},
          {"math_factorial", math_factorial}},
         {{"pi_str", [] { return make_str(PI_100_STR); }},
          {"pi", [] { return Value::number(3.141592653589793); }},
          {"e", [] { return Value::number(2.718281828459045); }}}},
        {"time", {{"time_sleep", time_sleep}, {"time_now", time_now}, {"time_format", time_format}}, {}},
        {"sys",
         {{"file_write", file_write}, {"file_read", file_read}, {"file_exists", file_exists},
          {"file_delete", file_delete}, {"sys_args", sys_args}, {"sys_execute", sys_execute},
          {"os_name", os_name}, {"os_listdir", os_listdir}},
         {}},
        {"random",
         {{"random", rnd_random}, {"seed", rnd_seed}, {"randint", rnd_randint}, {"randrange", rnd_randrange},
          {"uniform", rnd_uniform}, {"choice", rnd_choice}, {"shuffle", rnd_shuffle}},
         {}},
        {"builtins",
         {{"len", b_len}, {"str", b_str}, {"int", b_int}, {"float", b_float}, {"bool", b_bool}, {"list", b_list},
          {"min", b_min}, {"max", b_max}, {"sum", b_sum}, {"range", b_range}, {"sorted", b_sorted},
          {"round", b_round}, {"abs", b_abs}, {"print", b_print}, {"input", b_input}, {"type", b_type}},
         {{"True", [] { return Value::boolean(true); }}, {"False", [] { return Value::boolean(false); }},
          {"None", [] { return Value::none(); }}}},
        {"window",
         {{"window_open", qt_gui::window_open}, {"window_is_open", qt_gui::window_is_open},
          {"window_poll_events", qt_gui::window_poll_events}, {"window_clear", qt_gui::window_clear},
          {"window_draw_rect", qt_gui::window_draw_rect}, {"window_draw_circle", qt_gui::window_draw_circle},
          {"window_draw_line", qt_gui::window_draw_line}, {"window_draw_text", qt_gui::window_draw_text},
          {"window_present", qt_gui::window_present}, {"window_close", qt_gui::window_close},
          {"window_set_title", qt_gui::window_set_title}},
         {}},
        {"mac_window",
         {{"window_open", qt_gui::window_open}, {"window_is_open", qt_gui::window_is_open},
          {"window_poll_events", qt_gui::window_poll_events}, {"window_clear", qt_gui::window_clear},
          {"window_draw_rect", qt_gui::window_draw_rect}, {"window_draw_circle", qt_gui::window_draw_circle},
          {"window_draw_line", qt_gui::window_draw_line}, {"window_draw_text", qt_gui::window_draw_text},
          {"window_present", qt_gui::window_present}, {"window_close", qt_gui::window_close},
          {"window_set_title", qt_gui::window_set_title}},
         {}},
        {"qt",
         {{"qt_app", qt_gui::qt_app}, {"qt_window", qt_gui::qt_window}, {"qt_show", qt_gui::qt_show},
          {"qt_hide", qt_gui::qt_hide}, {"qt_close", qt_gui::qt_close}, {"qt_run", qt_gui::qt_run},
          {"qt_quit", qt_gui::qt_quit}, {"qt_vbox", qt_gui::qt_vbox}, {"qt_hbox", qt_gui::qt_hbox},
          {"qt_frame", qt_gui::qt_frame}, {"qt_groupbox", qt_gui::qt_groupbox}, {"qt_label", qt_gui::qt_label},
          {"qt_button", qt_gui::qt_button}, {"qt_input", qt_gui::qt_input}, {"qt_textarea", qt_gui::qt_textarea},
          {"qt_checkbox", qt_gui::qt_checkbox}, {"qt_slider", qt_gui::qt_slider},
          {"qt_progressbar", qt_gui::qt_progressbar}, {"qt_combobox", qt_gui::qt_combobox},
          {"qt_listwidget", qt_gui::qt_listwidget}, {"qt_set_text", qt_gui::qt_set_text},
          {"qt_get_text", qt_gui::qt_get_text}, {"qt_set_value", qt_gui::qt_set_value},
          {"qt_get_value", qt_gui::qt_get_value}, {"qt_is_checked", qt_gui::qt_is_checked},
          {"qt_set_checked", qt_gui::qt_set_checked}, {"qt_set_style", qt_gui::qt_set_style},
          {"qt_set_title", qt_gui::qt_set_title}, {"qt_resize", qt_gui::qt_resize},
          {"qt_set_enabled", qt_gui::qt_set_enabled}, {"qt_add_item", qt_gui::qt_add_item},
          {"qt_clear_items", qt_gui::qt_clear_items}, {"qt_on_click", qt_gui::qt_on_click},
          {"qt_on_change", qt_gui::qt_on_change}, {"qt_timer", qt_gui::qt_timer},
          {"qt_msgbox", qt_gui::qt_msgbox}, {"qt_confirm", qt_gui::qt_confirm},
          {"qt_file_dialog", qt_gui::qt_file_dialog}, {"qt_color_dialog", qt_gui::qt_color_dialog},
          // Also expose canvas drawing inside <qt>
          {"window_open", qt_gui::window_open}, {"window_is_open", qt_gui::window_is_open},
          {"window_poll_events", qt_gui::window_poll_events}, {"window_clear", qt_gui::window_clear},
          {"window_draw_rect", qt_gui::window_draw_rect}, {"window_draw_circle", qt_gui::window_draw_circle},
          {"window_draw_line", qt_gui::window_draw_line}, {"window_draw_text", qt_gui::window_draw_text},
          {"window_present", qt_gui::window_present}, {"window_close", qt_gui::window_close}},
         {}},
        {"gui",
         {{"qt_app", qt_gui::qt_app}, {"qt_window", qt_gui::qt_window}, {"qt_show", qt_gui::qt_show},
          {"qt_hide", qt_gui::qt_hide}, {"qt_close", qt_gui::qt_close}, {"qt_run", qt_gui::qt_run},
          {"qt_quit", qt_gui::qt_quit}, {"qt_vbox", qt_gui::qt_vbox}, {"qt_hbox", qt_gui::qt_hbox},
          {"qt_frame", qt_gui::qt_frame}, {"qt_groupbox", qt_gui::qt_groupbox}, {"qt_label", qt_gui::qt_label},
          {"qt_button", qt_gui::qt_button}, {"qt_input", qt_gui::qt_input}, {"qt_textarea", qt_gui::qt_textarea},
          {"qt_checkbox", qt_gui::qt_checkbox}, {"qt_slider", qt_gui::qt_slider},
          {"qt_progressbar", qt_gui::qt_progressbar}, {"qt_combobox", qt_gui::qt_combobox},
          {"qt_listwidget", qt_gui::qt_listwidget}, {"qt_set_text", qt_gui::qt_set_text},
          {"qt_get_text", qt_gui::qt_get_text}, {"qt_set_value", qt_gui::qt_set_value},
          {"qt_get_value", qt_gui::qt_get_value}, {"qt_is_checked", qt_gui::qt_is_checked},
          {"qt_set_checked", qt_gui::qt_set_checked}, {"qt_set_style", qt_gui::qt_set_style},
          {"qt_set_title", qt_gui::qt_set_title}, {"qt_resize", qt_gui::qt_resize},
          {"qt_set_enabled", qt_gui::qt_set_enabled}, {"qt_add_item", qt_gui::qt_add_item},
          {"qt_clear_items", qt_gui::qt_clear_items}, {"qt_on_click", qt_gui::qt_on_click},
          {"qt_on_change", qt_gui::qt_on_change}, {"qt_timer", qt_gui::qt_timer},
          {"qt_msgbox", qt_gui::qt_msgbox}, {"qt_confirm", qt_gui::qt_confirm},
          {"qt_file_dialog", qt_gui::qt_file_dialog}, {"qt_color_dialog", qt_gui::qt_color_dialog}},
         {}},
        {"ui_engine",
         {{"qt_app", qt_gui::qt_app}, {"qt_window", qt_gui::qt_window}, {"qt_show", qt_gui::qt_show},
          {"qt_hide", qt_gui::qt_hide}, {"qt_close", qt_gui::qt_close}, {"qt_run", qt_gui::qt_run},
          {"qt_quit", qt_gui::qt_quit}, {"qt_vbox", qt_gui::qt_vbox}, {"qt_hbox", qt_gui::qt_hbox},
          {"qt_frame", qt_gui::qt_frame}, {"qt_groupbox", qt_gui::qt_groupbox}, {"qt_label", qt_gui::qt_label},
          {"qt_button", qt_gui::qt_button}, {"qt_input", qt_gui::qt_input}, {"qt_textarea", qt_gui::qt_textarea},
          {"qt_checkbox", qt_gui::qt_checkbox}, {"qt_slider", qt_gui::qt_slider},
          {"qt_progressbar", qt_gui::qt_progressbar}, {"qt_combobox", qt_gui::qt_combobox},
          {"qt_listwidget", qt_gui::qt_listwidget}, {"qt_set_text", qt_gui::qt_set_text},
          {"qt_get_text", qt_gui::qt_get_text}, {"qt_set_value", qt_gui::qt_set_value},
          {"qt_get_value", qt_gui::qt_get_value}, {"qt_is_checked", qt_gui::qt_is_checked},
          {"qt_set_checked", qt_gui::qt_set_checked}, {"qt_set_style", qt_gui::qt_set_style},
          {"qt_set_title", qt_gui::qt_set_title}, {"qt_resize", qt_gui::qt_resize},
          {"qt_set_enabled", qt_gui::qt_set_enabled}, {"qt_add_item", qt_gui::qt_add_item},
          {"qt_clear_items", qt_gui::qt_clear_items}, {"qt_on_click", qt_gui::qt_on_click},
          {"qt_on_change", qt_gui::qt_on_change}, {"qt_timer", qt_gui::qt_timer},
          {"qt_msgbox", qt_gui::qt_msgbox}, {"qt_confirm", qt_gui::qt_confirm},
          {"qt_file_dialog", qt_gui::qt_file_dialog}, {"qt_color_dialog", qt_gui::qt_color_dialog}},
         {}},
    };
    return mods;
}

inline const ModuleDef* find_module(const std::string& name) {
    for (auto& m : modules()) if (name == m.name) return &m;
    return nullptr;
}

// Returns the value bound to `name` by module `mod` (used by compiled code).
inline Value module_value(const char* mod, const char* name) {
    const ModuleDef* m = find_module(mod);
    if (m) {
        for (auto& f : m->functions) if (std::strcmp(f.name, name) == 0) return make_func(f.name, -1, f.fn, nullptr, false, true);
        for (auto& c : m->constants) if (std::strcmp(c.name, name) == 0) return c.make();
    }
    fail(std::string("Module '") + mod + "' has no member '" + name + "'");
}

}  // namespace natives

// ------------------------------------------------------------------------------
// Error reporting (same banners as the original nix.py)
// ------------------------------------------------------------------------------
#define NIX_RED "\033[91m"
#define NIX_YELLOW "\033[93m"
#define NIX_RESET "\033[0m"
#define NIX_BOLD "\033[1m"

inline void report_runtime_error(const RuntimeError& e) {
    fflush(stdout);
    if (e.internal) {
        printf("\n" NIX_RED NIX_BOLD "[Internal Interpreter Error]" NIX_RESET "\n");
        fflush(stdout);
        fprintf(stderr, NIX_RED "%s" NIX_RESET "\n", e.what());
    } else {
        printf("\n" NIX_RED NIX_BOLD "[Nix Execution Halted]" NIX_RESET "\n");
        fflush(stdout);
        fprintf(stderr, NIX_RED "Nix Runtime Error: %s" NIX_RESET "\n", e.what());
    }
}
inline void report_internal(const std::string& msg) {
    fflush(stdout);
    printf("\n" NIX_RED NIX_BOLD "[Internal Interpreter Error]" NIX_RESET "\n");
    fflush(stdout);
    fprintf(stderr, NIX_RED "%s" NIX_RESET "\n", msg.c_str());
}

}  // namespace nix

#endif  // NIX_RUNTIME_HPP

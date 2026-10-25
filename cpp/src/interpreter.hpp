// ==============================================================================
// NIX INTERPRETER (C++ port of evaluator.py / environment.py / nix_builtins.py /
// modules.py)
//
// Scoping rules are identical to the Python implementation:
//   * global environment
//   * function call   -> new environment whose parent is the GLOBAL env
//   * `for` loop      -> child environment holding the iterator
//   * class instance  -> environment whose variables are the instance fields
//   * `x = v`         -> assigns x where it is already defined in the chain,
//                        otherwise defines it in the current environment.
// Extension: methods (functions defined in a class body) called through
// `obj.method()` can read/write the instance fields directly.
// ==============================================================================
#pragma once

#include <set>
#include <string>
#include <vector>

#include "frontend.hpp"

namespace nix {

class Interpreter {
public:
    struct Env {
        Env* parent = nullptr;
        InstanceObj* inst = nullptr;  // non-null for instance environments
        const Value* inst_val = nullptr;
        std::vector<std::pair<const Sym*, Value>> vars;
    };

    std::string script_dir;
    int error_line = 0;

    Interpreter() {
        self_ptr() = this;
        interp_hook() = &Interpreter::hook;
    }

    // Runs a whole program in the global environment.
    void run(const ProgramNode* prog) {
        if (exec_block(prog, nullptr)) {
            Value v = std::move(ret_val_);
            throw TopLevelReturn{v};
        }
    }

private:
    std::vector<Value> globals_;
    Value ret_val_;
    std::set<std::string> loaded_;
    std::vector<std::unique_ptr<ProgramNode>> modules_;
    int depth_ = 0;

    static Interpreter*& self_ptr() {
        static Interpreter* p = nullptr;
        return p;
    }

    // Called by the runtime for interpreted functions and class bodies.
    static Value hook(const Value& callee, const Value* args, size_t n, const Value& self) {
        Interpreter* in = self_ptr();
        if (callee.tag == Tag::Class) {
            in->run_class_body(as_class(callee), self);
            return Value::none();
        }
        return in->call_user(as_func(callee), args, n, self);
    }

    // ---------------------------------------------------------------- env ops
    NIX_INLINE Value* global_slot(const Sym* s) {
        if ((size_t)s->id >= globals_.size()) globals_.resize(sym_list().size() + 64);
        return &globals_[s->id];
    }

    Value lookup(const Sym* s, Env* env) {
        for (Env* e = env; e; e = e->parent) {
            if (e->inst) {
                if (Value* p = e->inst->find(s)) return bind_if_method(*e->inst_val, *p);
            } else {
                for (auto& kv : e->vars)
                    if (kv.first == s) return kv.second;
            }
        }
        if ((size_t)s->id < globals_.size() && !globals_[s->id].undef()) return globals_[s->id];
        undefined_var(s->name);
    }

    void define(const Sym* s, const Value& v, Env* env) {
        if (!env) { *global_slot(s) = v; return; }
        if (env->inst) { env->inst->set(s, v); return; }
        for (auto& kv : env->vars)
            if (kv.first == s) { kv.second = v; return; }
        env->vars.emplace_back(s, v);
    }

    void assign_name(const Sym* s, const Value& v, Env* env) {
        for (Env* e = env; e; e = e->parent) {
            if (e->inst) {
                if (Value* p = e->inst->find(s)) { *p = v; return; }
            } else {
                for (auto& kv : e->vars)
                    if (kv.first == s) { kv.second = v; return; }
            }
        }
        Value* g = global_slot(s);
        if (!g->undef() || !env) { *g = v; return; }
        define(s, v, env);
    }

    // ---------------------------------------------------------------- calls
    Value call_user(FuncObj* f, const Value* args, size_t n, const Value& self) {
        const FuncDefNode* fd = static_cast<const FuncDefNode*>(f->node);
        if (n != fd->params.size())
            fail("Function '" + fd->name + "' expects " + std::to_string(fd->params.size()) + " arguments, but received " +
                 std::to_string(n));
        if (++depth_ > 100000) { depth_ = 0; fail_internal("maximum recursion depth exceeded"); }
        Env inst_env;
        Env local;
        if (f->is_method && self.tag == Tag::Instance) {
            inst_env.inst = as_inst(self);
            inst_env.inst_val = &self;
            local.parent = &inst_env;
        }
        local.vars.reserve(n + 4);
        for (size_t k = 0; k < n; ++k) local.vars.emplace_back(fd->param_syms[k], args[k]);
        bool returned = exec_block(fd->body.get(), &local);
        --depth_;
        if (returned) {
            Value v = std::move(ret_val_);
            ret_val_ = Value();
            return v.undef() ? Value::none() : v;
        }
        return Value::none();
    }

    void run_class_body(ClassObj* c, const Value& inst) {
        const ClassDefNode* cd = static_cast<const ClassDefNode*>(c->node);
        Env env;
        env.inst = as_inst(inst);
        env.inst_val = &inst;
        if (exec_block(cd->body.get(), &env)) ret_val_ = Value();
    }

    NIX_INLINE Value invoke(const Value& f, const Value* args, size_t n) {
        if (f.tag == Tag::Func && !as_func(f)->fn) {
            static const Value no_self = Value::none();
            return call_user(as_func(f), args, n, no_self);
        }
        return call(f, args, n);
    }

    // ---------------------------------------------------------------- statements
    bool exec_block(const ProgramNode* b, Env* env) {
        for (auto& s : b->stmts)
            if (exec(s.get(), env)) return true;
        return false;
    }

    bool exec(const Node* n, Env* env) {
        try {
            return exec_inner(n, env);
        } catch (RuntimeError&) {
            if (!error_line) error_line = n->line;
            throw;
        }
    }

    bool exec_inner(const Node* n, Env* env) {
        switch (n->kind) {
            case NK::Program: return exec_block(as<ProgramNode>(n), env);
            case NK::Assign: exec_assign(as<AssignNode>(n), env); return false;
            case NK::FuncDef: {
                auto* fd = as<FuncDefNode>(n);
                bool method = env && env->inst;
                define(fd->sym, make_func(fd->name, (int)fd->params.size(), nullptr, fd, method, false), env);
                return false;
            }
            case NK::ClassDef: {
                auto* cd = as<ClassDefNode>(n);
                define(cd->sym, make_class(cd->name, nullptr, cd), env);
                return false;
            }
            case NK::If: {
                auto* in = as<IfNode>(n);
                if (truthy(eval(in->cond.get(), env))) return exec_block(in->then_b.get(), env);
                if (in->else_b) return exec(in->else_b.get(), env);
                return false;
            }
            case NK::While: {
                auto* w = as<WhileNode>(n);
                while (truthy(eval(w->cond.get(), env)))
                    if (exec_block(w->body.get(), env)) return true;
                return false;
            }
            case NK::For: {
                auto* f = as<ForNode>(n);
                Value lim = eval(f->limit.get(), env);
                int64_t count = loop_count(lim);
                Env local;
                local.parent = env;
                for (int64_t k = 0; k < count; ++k) {
                    define(f->iter_sym, Value::integer(k), &local);
                    if (exec_block(f->body.get(), &local)) return true;
                }
                return false;
            }
            case NK::Return: {
                auto* r = as<ReturnNode>(n);
                ret_val_ = r->expr ? eval(r->expr.get(), env) : Value::none();
                return true;
            }
            case NK::Out: {
                auto* o = as<OutNode>(n);
                size_t c = o->args.size();
                if (c <= 8) {
                    Value tmp[8];
                    for (size_t k = 0; k < c; ++k) tmp[k] = eval(o->args[k].get(), env);
                    out(tmp, c);
                } else {
                    std::vector<Value> tmp(c);
                    for (size_t k = 0; k < c; ++k) tmp[k] = eval(o->args[k].get(), env);
                    out(tmp.data(), c);
                }
                return false;
            }
            case NK::Using: do_using(as<UsingNode>(n)->lib); return false;
            default: eval(n, env); return false;
        }
    }

    void exec_assign(const AssignNode* a, Env* env) {
        Value v = eval(a->value.get(), env);
        const Node* t = a->target.get();
        if (t->kind == NK::Ident) {
            assign_name(as<IdentNode>(t)->sym, v, env);
        } else if (t->kind == NK::GetAttr) {
            auto* g = as<GetAttrNode>(t);
            Value obj = eval(g->obj.get(), env);
            setattr(obj, g->sym, v);
        } else {
            auto* s = as<SubscriptNode>(t);
            Value coll = eval(s->coll.get(), env);
            Value idx = eval(s->index.get(), env);
            setitem(coll, idx, v);
        }
    }

    void do_using(const std::string& lib) {
        if (const natives::ModuleDef* m = natives::find_module(lib)) {
            if (!loaded_.insert("native:" + lib).second) return;
            for (auto& c : m->constants) *global_slot(intern(c.name)) = c.make();
            for (auto& f : m->functions) *global_slot(intern(f.name)) = make_func(f.name, -1, f.fn, nullptr, false, true);
            return;
        }
        std::string file = lib + ".nx";
        std::string path;
        if (natives::path_exists(file)) path = file;
        else if (!script_dir.empty() && natives::path_exists(script_dir + "/" + file)) path = script_dir + "/" + file;
        if (path.empty())
            fail("Module Import Error: Cannot find module '" + lib + "' locally or in native libraries");
        if (!loaded_.insert(file).second) return;
        try {
            std::string src = natives::read_file_or_fail(path);
            modules_.push_back(parse_source(src));
            int saved_line = error_line;
            if (exec_block(modules_.back().get(), nullptr)) {
                ret_val_ = Value();
                fail("return outside of a function");
            }
            error_line = saved_line;
        } catch (SyntaxError& e) {
            loaded_.erase(file);
            fail("Failed inside Nix module '" + lib + "': " + e.what());
        } catch (RuntimeError& e) {
            loaded_.erase(file);
            error_line = 0;
            fail("Failed inside Nix module '" + lib + "': Nix Runtime Error: " + e.what());
        }
    }

    // ---------------------------------------------------------------- expressions
    Value eval(const Node* n, Env* env) {
        switch (n->kind) {
            case NK::Literal: return as<LiteralNode>(n)->value;
            case NK::Ident: return lookup(as<IdentNode>(n)->sym, env);
            case NK::BinOp: {
                auto* b = as<BinOpNode>(n);
                Value l = eval(b->left.get(), env);
                Value r = eval(b->right.get(), env);
                switch (b->op) {
                    case Op::Add: return add(l, r);
                    case Op::Sub: return sub(l, r);
                    case Op::Mul: return mul(l, r);
                    case Op::Div: return divide(l, r);
                    case Op::Mod: return mod(l, r);
                    case Op::Eq: return eq(l, r);
                    case Op::Ne: return ne(l, r);
                    case Op::Lt: return lt(l, r);
                    case Op::Gt: return gt(l, r);
                    case Op::Le: return le(l, r);
                    case Op::Ge: return ge(l, r);
                }
                return Value::none();
            }
            case NK::Unary: return neg(eval(as<UnaryNode>(n)->expr.get(), env));
            case NK::Call: return eval_call(as<CallNode>(n), env);
            case NK::Cin: {
                auto* c = as<CinNode>(n);
                if (c->prompt) {
                    Value p = eval(c->prompt.get(), env);
                    return cin_read(&p);
                }
                return cin_read(nullptr);
            }
            case NK::Vector: {
                auto* v = as<VectorNode>(n);
                Value lst = make_list();
                auto& items = as_list(lst)->items;
                items.reserve(v->elems.size());
                for (auto& e : v->elems) items.push_back(eval(e.get(), env));
                return lst;
            }
            case NK::Dict: {
                auto* d = as<DictNode>(n);
                Value dict = make_dict();
                for (auto& kv : d->pairs) {
                    Value k = eval(kv.first.get(), env);
                    if (k.tag == Tag::List || k.tag == Tag::Dict)
                        fail("Dictionary key must be a primitive hashable type (string, number, boolean)");
                    Value v = eval(kv.second.get(), env);
                    as_dict(dict)->set(k, v);
                }
                return dict;
            }
            case NK::Subscript: {
                auto* s = as<SubscriptNode>(n);
                Value c = eval(s->coll.get(), env);
                Value i = eval(s->index.get(), env);
                return getitem(c, i);
            }
            case NK::GetAttr: {
                auto* g = as<GetAttrNode>(n);
                return getattr(eval(g->obj.get(), env), g->sym);
            }
            default: break;
        }
        fail("Unhandled AST Node type");
    }

    template <class F>
    Value with_args(const std::vector<NodeP>& args, Env* env, F&& fn) {
        size_t c = args.size();
        if (c <= 6) {
            Value tmp[6];
            for (size_t k = 0; k < c; ++k) tmp[k] = eval(args[k].get(), env);
            return fn(tmp, c);
        }
        std::vector<Value> tmp(c);
        for (size_t k = 0; k < c; ++k) tmp[k] = eval(args[k].get(), env);
        return fn(tmp.data(), c);
    }

    Value eval_call(const CallNode* c, Env* env) {
        const Node* t = c->target.get();
        if (t->kind == NK::GetAttr) {
            auto* g = as<GetAttrNode>(t);
            Value obj = eval(g->obj.get(), env);
            if (obj.tag == Tag::Instance) {
                Value* p = as_inst(obj)->find(g->sym);
                if (!p) fail("Property '" + g->member + "' does not exist on this object");
                Value f = *p;
                if (f.tag == Tag::Class) return instantiate(f);
                bool method = f.tag == Tag::Func && as_func(f)->is_method;
                return with_args(c->args, env, [&](const Value* a, size_t n) {
                    if (method) {
                        if (!as_func(f)->fn) return call_user(as_func(f), a, n, obj);
                        return call_with_self(f, a, n, obj);
                    }
                    return invoke(f, a, n);
                });
            }
            if (!has_builtin_method(obj, g->member))
                fail("Cannot read property '" + g->member + "' on object of type '" + type_name(obj) + "'");
            return with_args(c->args, env, [&](const Value* a, size_t n) { return call_builtin_method(obj, g->sym, a, n); });
        }
        Value f = t->kind == NK::Ident ? lookup(as<IdentNode>(t)->sym, env) : eval(t, env);
        if (f.tag == Tag::Class) return instantiate(f);  // arguments are ignored (like the original)
        if (f.tag != Tag::Func && f.tag != Tag::Bound) {
            std::string what = t->kind == NK::Ident ? as<IdentNode>(t)->name : to_display(f);
            fail("'" + what + "' is not callable");
        }
        return with_args(c->args, env, [&](const Value* a, size_t n) { return invoke(f, a, n); });
    }
};

}  // namespace nix

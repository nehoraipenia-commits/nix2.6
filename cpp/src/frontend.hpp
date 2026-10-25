// ==============================================================================
// NIX FRONT-END: Lexer, AST and Parser (C++ port of lexer.py / ast_nodes.py /
// nix_parser.py). Same grammar, same error messages.
// ==============================================================================
#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "nix_runtime.hpp"

namespace nix {

struct SyntaxError : std::runtime_error {
    explicit SyntaxError(const std::string& m) : std::runtime_error(m) {}
};

// ------------------------------------------------------------------------------
// Tokens
// ------------------------------------------------------------------------------
enum class TT {
    IDENTIFIER, NUMBER, STRING,
    FCT, IF, ELSE, WHILE, FOR, OUT, CIN, USING, TRUE_, FALSE_, RETURN, CLASS,
    ASSIGN, PLUS, MINUS, MUL, DIV, MOD,
    EQ, NE, LT, GT, LTE, GTE,
    LPAREN, RPAREN, LBRACE, RBRACE, COMMA, LBRACK, RBRACK, COLON,
    LANGLE, RANGLE, DOT,
    EOF_
};

inline const char* tt_name(TT t) {
    static const char* names[] = {"IDENTIFIER", "NUMBER", "STRING", "FCT", "IF", "ELSE", "WHILE", "FOR", "OUT", "CIN",
                                  "USING", "TRUE", "FALSE", "RETURN", "CLASS", "ASSIGN", "PLUS", "MINUS", "MUL", "DIV",
                                  "MOD", "EQ", "NE", "LT", "GT", "LTE", "GTE", "LPAREN", "RPAREN", "LBRACE", "RBRACE",
                                  "COMMA", "LBRACK", "RBRACK", "COLON", "LANGLE", "RANGLE", "DOT", "EOF"};
    return names[(int)t];
}

struct Token {
    TT type;
    std::string text;   // identifier / string / operator text
    Value num;          // NUMBER value (int or float)
    int line, column;
};

// Python repr() of a token value for parser error messages.
inline std::string token_repr(const Token& t) {
    if (t.type == TT::EOF_) return "None";
    if (t.type == TT::NUMBER) return to_display(t.num);
    return "'" + t.text + "'";
}

// ------------------------------------------------------------------------------
// Lexer
// ------------------------------------------------------------------------------
class Lexer {
public:
    explicit Lexer(const std::string& src) : src_(src) {}

    std::vector<Token> tokenize() {
        std::vector<Token> toks;
        while (pos_ < src_.size()) {
            unsigned char c = (unsigned char)src_[pos_];
            if (isspace(c)) { advance(); continue; }
            if (c == '#' || (c == '/' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '/')) {
                while (pos_ < src_.size() && src_[pos_] != '\n') advance();
                continue;
            }
            if (isdigit(c)) {
                int sc = col_;
                std::string s;
                while (pos_ < src_.size() && (isdigit((unsigned char)src_[pos_]) || src_[pos_] == '.')) s += advance();
                Token t{TT::NUMBER, s, Value(), line_, sc};
                if (s.find('.') != std::string::npos) {
                    double d;
                    if (std::count(s.begin(), s.end(), '.') > 1 || !py_parse_float(s, d))
                        error("could not convert string to float: '" + s + "'");
                    t.num = Value::number(d);
                } else {
                    errno = 0;
                    long long v = strtoll(s.c_str(), nullptr, 10);
                    t.num = errno == ERANGE ? Value::number(strtod(s.c_str(), nullptr)) : Value::integer(v);
                }
                toks.push_back(std::move(t));
                continue;
            }
            if (isalpha(c) || c == '_' || c >= 0x80) {
                int sc = col_;
                std::string s;
                while (pos_ < src_.size()) {
                    unsigned char d = (unsigned char)src_[pos_];
                    if (!(isalnum(d) || d == '_' || d >= 0x80)) break;
                    s += advance();
                }
                toks.push_back(Token{keyword(s), s, Value(), line_, sc});
                continue;
            }
            if (c == '"' || c == '\'') {
                char q = advance();
                int sc = col_;
                std::string s;
                while (pos_ < src_.size() && src_[pos_] != q) s += advance();
                if (pos_ >= src_.size()) error("Unterminated string literal");
                advance();
                toks.push_back(Token{TT::STRING, s, Value(), line_, sc});
                continue;
            }
            int sc = col_;
            auto two = [&](char next, TT yes, const char* ytxt, TT no, const char* ntxt) {
                advance();
                if (pos_ < src_.size() && src_[pos_] == next) { advance(); toks.push_back(Token{yes, ytxt, Value(), line_, sc}); }
                else toks.push_back(Token{no, ntxt, Value(), line_, sc});
            };
            if (c == '=') { two('=', TT::EQ, "==", TT::ASSIGN, "="); continue; }
            if (c == '<') { two('=', TT::LTE, "<=", TT::LANGLE, "<"); continue; }
            if (c == '>') { two('=', TT::GTE, ">=", TT::RANGLE, ">"); continue; }
            if (c == '!') {
                advance();
                if (pos_ < src_.size() && src_[pos_] == '=') { advance(); toks.push_back(Token{TT::NE, "!=", Value(), line_, sc}); }
                else error("Unexpected character '!' without '='");
                continue;
            }
            TT st;
            switch (c) {
                case '+': st = TT::PLUS; break;
                case '-': st = TT::MINUS; break;
                case '*': st = TT::MUL; break;
                case '/': st = TT::DIV; break;
                case '%': st = TT::MOD; break;
                case '(': st = TT::LPAREN; break;
                case ')': st = TT::RPAREN; break;
                case '{': st = TT::LBRACE; break;
                case '}': st = TT::RBRACE; break;
                case ',': st = TT::COMMA; break;
                case '[': st = TT::LBRACK; break;
                case ']': st = TT::RBRACK; break;
                case ':': st = TT::COLON; break;
                case '.': st = TT::DOT; break;
                default: error(std::string("Unexpected character: '") + (char)c + "'");
            }
            toks.push_back(Token{st, std::string(1, (char)c), Value(), line_, col_});
            advance();
        }
        toks.push_back(Token{TT::EOF_, "", Value(), line_, col_});
        return toks;
    }

private:
    const std::string& src_;
    size_t pos_ = 0;
    int line_ = 1, col_ = 1;

    char advance() {
        char c = src_[pos_++];
        if (c == '\n') { ++line_; col_ = 1; } else ++col_;
        return c;
    }
    [[noreturn]] void error(const std::string& m) {
        throw SyntaxError("Nix Lexer Error: " + m + " at line " + std::to_string(line_) + ", column " + std::to_string(col_));
    }
    static TT keyword(const std::string& s) {
        static const std::pair<const char*, TT> kws[] = {
            {"fct", TT::FCT}, {"class", TT::CLASS}, {"if", TT::IF}, {"else", TT::ELSE}, {"while", TT::WHILE},
            {"for", TT::FOR}, {"out", TT::OUT}, {"cin", TT::CIN}, {"using", TT::USING}, {"true", TT::TRUE_},
            {"false", TT::FALSE_}, {"return", TT::RETURN}};
        for (auto& k : kws) if (s == k.first) return k.second;
        return TT::IDENTIFIER;
    }
};

// ------------------------------------------------------------------------------
// AST
// ------------------------------------------------------------------------------
enum class NK {
    Program, Assign, FuncDef, ClassDef, If, While, For, Return, Out, Using,
    BinOp, Unary, Literal, Ident, Call, Cin, Vector, Dict, Subscript, GetAttr
};
enum class Op { Add, Sub, Mul, Div, Mod, Eq, Ne, Lt, Gt, Le, Ge };

inline const char* op_text(Op o) {
    static const char* t[] = {"+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">="};
    return t[(int)o];
}

struct Node {
    NK kind;
    int line;
    explicit Node(NK k, int l) : kind(k), line(l) {}
    virtual ~Node() = default;
};
using NodeP = std::unique_ptr<Node>;

struct ProgramNode : Node {
    std::vector<NodeP> stmts;
    explicit ProgramNode(int l) : Node(NK::Program, l) {}
};
struct AssignNode : Node {
    NodeP target, value;
    AssignNode(NodeP t, NodeP v, int l) : Node(NK::Assign, l), target(std::move(t)), value(std::move(v)) {}
};
struct FuncDefNode : Node {
    std::string name;
    const Sym* sym;
    std::vector<std::string> params;
    std::vector<const Sym*> param_syms;
    std::unique_ptr<ProgramNode> body;
    bool is_method = false;  // set by the interpreter/compiler when inside a class body
    FuncDefNode(int l) : Node(NK::FuncDef, l) {}
};
struct ClassDefNode : Node {
    std::string name;
    const Sym* sym;
    std::unique_ptr<ProgramNode> body;
    ClassDefNode(int l) : Node(NK::ClassDef, l) {}
};
struct IfNode : Node {
    NodeP cond;
    std::unique_ptr<ProgramNode> then_b;
    NodeP else_b;  // ProgramNode or IfNode or null
    IfNode(int l) : Node(NK::If, l) {}
};
struct WhileNode : Node {
    NodeP cond;
    std::unique_ptr<ProgramNode> body;
    WhileNode(int l) : Node(NK::While, l) {}
};
struct ForNode : Node {
    std::string iter;
    const Sym* iter_sym;
    NodeP limit;
    std::unique_ptr<ProgramNode> body;
    ForNode(int l) : Node(NK::For, l) {}
};
struct ReturnNode : Node {
    NodeP expr;
    ReturnNode(NodeP e, int l) : Node(NK::Return, l), expr(std::move(e)) {}
};
struct OutNode : Node {
    std::vector<NodeP> args;
    OutNode(int l) : Node(NK::Out, l) {}
};
struct UsingNode : Node {
    std::string lib;
    UsingNode(std::string s, int l) : Node(NK::Using, l), lib(std::move(s)) {}
};
struct BinOpNode : Node {
    NodeP left, right;
    Op op;
    BinOpNode(NodeP a, Op o, NodeP b, int l) : Node(NK::BinOp, l), left(std::move(a)), right(std::move(b)), op(o) {}
};
struct UnaryNode : Node {
    NodeP expr;
    UnaryNode(NodeP e, int l) : Node(NK::Unary, l), expr(std::move(e)) {}
};
struct LiteralNode : Node {
    Value value;
    LiteralNode(Value v, int l) : Node(NK::Literal, l), value(std::move(v)) {}
};
struct IdentNode : Node {
    std::string name;
    const Sym* sym;
    IdentNode(std::string n, int l) : Node(NK::Ident, l), name(std::move(n)), sym(intern(name)) {}
};
struct CallNode : Node {
    NodeP target;
    std::vector<NodeP> args;
    CallNode(NodeP t, int l) : Node(NK::Call, l), target(std::move(t)) {}
};
struct CinNode : Node {
    NodeP prompt;
    CinNode(NodeP p, int l) : Node(NK::Cin, l), prompt(std::move(p)) {}
};
struct VectorNode : Node {
    std::vector<NodeP> elems;
    VectorNode(int l) : Node(NK::Vector, l) {}
};
struct DictNode : Node {
    std::vector<std::pair<NodeP, NodeP>> pairs;
    DictNode(int l) : Node(NK::Dict, l) {}
};
struct SubscriptNode : Node {
    NodeP coll, index;
    SubscriptNode(NodeP c, NodeP i, int l) : Node(NK::Subscript, l), coll(std::move(c)), index(std::move(i)) {}
};
struct GetAttrNode : Node {
    NodeP obj;
    std::string member;
    const Sym* sym;
    GetAttrNode(NodeP o, std::string m, int l) : Node(NK::GetAttr, l), obj(std::move(o)), member(std::move(m)), sym(intern(member)) {}
};

template <class T>
inline T* as(const Node* n) { return static_cast<T*>(const_cast<Node*>(n)); }

// ------------------------------------------------------------------------------
// Parser (recursive descent, same precedence as nix_parser.py)
// ------------------------------------------------------------------------------
class Parser {
public:
    explicit Parser(std::vector<Token> toks) : t_(std::move(toks)) {}

    std::unique_ptr<ProgramNode> parse() {
        auto prog = std::make_unique<ProgramNode>(1);
        while (!check(TT::EOF_)) prog->stmts.push_back(statement());
        return prog;
    }

private:
    std::vector<Token> t_;
    size_t p_ = 0;

    const Token& cur() const { return t_[p_]; }
    const Token& prev() const { return t_[p_ - 1]; }
    const Token& advance() {
        const Token& tk = t_[p_];
        if (tk.type != TT::EOF_) ++p_;
        return tk;
    }
    bool check(TT t) const { return cur().type == t; }
    bool match(TT t) { if (check(t)) { advance(); return true; } return false; }
    const Token& consume(TT t, const char* msg) {
        if (check(t)) return advance();
        error(msg);
    }
    [[noreturn]] void error(const std::string& msg) {
        const Token& tk = cur();
        throw SyntaxError("Nix Parser Error: " + msg + " at token " + tt_name(tk.type) + " (" + token_repr(tk) + ") on line " +
                          std::to_string(tk.line) + ", column " + std::to_string(tk.column));
    }

    NodeP statement() {
        int line = cur().line;
        if (match(TT::USING)) {
            consume(TT::LANGLE, "Expected '<' after 'using'");
            std::string lib = consume(TT::IDENTIFIER, "Expected module name inside '<>'").text;
            consume(TT::RANGLE, "Expected '>' after module name");
            return std::make_unique<UsingNode>(lib, line);
        }
        if (match(TT::CLASS)) {
            auto c = std::make_unique<ClassDefNode>(line);
            c->name = consume(TT::IDENTIFIER, "Expected class name after 'class'").text;
            c->sym = intern(c->name);
            c->body = block();
            return c;
        }
        if (match(TT::FCT)) {
            auto f = std::make_unique<FuncDefNode>(line);
            f->name = consume(TT::IDENTIFIER, "Expected function name after 'fct'").text;
            f->sym = intern(f->name);
            consume(TT::LPAREN, "Expected '(' after function name");
            if (!check(TT::RPAREN)) {
                f->params.push_back(consume(TT::IDENTIFIER, "Expected parameter name").text);
                while (match(TT::COMMA)) f->params.push_back(consume(TT::IDENTIFIER, "Expected parameter name").text);
            }
            consume(TT::RPAREN, "Expected ')' after parameter list");
            for (auto& p : f->params) f->param_syms.push_back(intern(p));
            f->body = block();
            return f;
        }
        if (match(TT::IF)) return if_statement(line);
        if (match(TT::WHILE)) {
            auto w = std::make_unique<WhileNode>(line);
            w->cond = expression();
            w->body = block();
            return w;
        }
        if (match(TT::FOR)) {
            auto f = std::make_unique<ForNode>(line);
            f->iter = consume(TT::IDENTIFIER, "Expected loop iterator variable name").text;
            f->iter_sym = intern(f->iter);
            consume(TT::MUL, "Expected '*' operator after iterator (e.g., 'for i * 10')");
            f->limit = expression();
            f->body = block();
            return f;
        }
        if (match(TT::RETURN)) {
            NodeP e;
            if (!check(TT::RBRACE) && !check(TT::EOF_)) e = expression();
            return std::make_unique<ReturnNode>(std::move(e), line);
        }
        if (match(TT::OUT)) {
            auto o = std::make_unique<OutNode>(line);
            consume(TT::LPAREN, "Expected '(' after 'out'");
            if (!check(TT::RPAREN)) {
                o->args.push_back(expression());
                while (match(TT::COMMA)) o->args.push_back(expression());
            }
            consume(TT::RPAREN, "Expected ')' after output arguments");
            return o;
        }
        NodeP e = expression();
        if (match(TT::ASSIGN)) {
            NodeP v = expression();
            if (e->kind == NK::Ident || e->kind == NK::Subscript || e->kind == NK::GetAttr)
                return std::make_unique<AssignNode>(std::move(e), std::move(v), line);
            error("Invalid assignment target. Left-hand side must be a variable, index subscript, or property.");
        }
        return e;
    }

    NodeP if_statement(int line) {
        auto n = std::make_unique<IfNode>(line);
        n->cond = expression();
        n->then_b = block();
        if (match(TT::ELSE)) {
            if (check(TT::IF)) {
                int l2 = cur().line;
                advance();
                n->else_b = if_statement(l2);
            } else {
                n->else_b = block();
            }
        }
        return n;
    }

    std::unique_ptr<ProgramNode> block() {
        auto b = std::make_unique<ProgramNode>(cur().line);
        consume(TT::LBRACE, "Expected '{' to start block");
        while (!check(TT::RBRACE) && !check(TT::EOF_)) b->stmts.push_back(statement());
        consume(TT::RBRACE, "Expected '}' to close block");
        return b;
    }

    NodeP expression() { return comparison(); }

    NodeP comparison() {
        NodeP left = additive();
        while (true) {
            Op op;
            if (check(TT::EQ)) op = Op::Eq;
            else if (check(TT::NE)) op = Op::Ne;
            else if (check(TT::LANGLE)) op = Op::Lt;
            else if (check(TT::RANGLE)) op = Op::Gt;
            else if (check(TT::LTE)) op = Op::Le;
            else if (check(TT::GTE)) op = Op::Ge;
            else break;
            int line = advance().line;
            NodeP right = additive();
            left = std::make_unique<BinOpNode>(std::move(left), op, std::move(right), line);
        }
        return left;
    }
    NodeP additive() {
        NodeP left = multiplicative();
        while (check(TT::PLUS) || check(TT::MINUS)) {
            Op op = check(TT::PLUS) ? Op::Add : Op::Sub;
            int line = advance().line;
            NodeP right = multiplicative();
            left = std::make_unique<BinOpNode>(std::move(left), op, std::move(right), line);
        }
        return left;
    }
    NodeP multiplicative() {
        NodeP left = unary();
        while (check(TT::MUL) || check(TT::DIV) || check(TT::MOD)) {
            Op op = check(TT::MUL) ? Op::Mul : check(TT::DIV) ? Op::Div : Op::Mod;
            int line = advance().line;
            NodeP right = unary();
            left = std::make_unique<BinOpNode>(std::move(left), op, std::move(right), line);
        }
        return left;
    }
    NodeP unary() {
        if (check(TT::MINUS)) {
            int line = advance().line;
            return std::make_unique<UnaryNode>(unary(), line);
        }
        return postfix();
    }
    NodeP postfix() {
        NodeP e = primary();
        while (true) {
            int line = cur().line;
            if (match(TT::LPAREN)) {
                auto c = std::make_unique<CallNode>(std::move(e), line);
                if (!check(TT::RPAREN)) {
                    c->args.push_back(expression());
                    while (match(TT::COMMA)) c->args.push_back(expression());
                }
                consume(TT::RPAREN, "Expected ')' after function arguments");
                e = std::move(c);
            } else if (match(TT::LBRACK)) {
                NodeP idx = expression();
                consume(TT::RBRACK, "Expected ']' after subscript index expression");
                e = std::make_unique<SubscriptNode>(std::move(e), std::move(idx), line);
            } else if (match(TT::DOT)) {
                std::string m = consume(TT::IDENTIFIER, "Expected property name after '.'").text;
                e = std::make_unique<GetAttrNode>(std::move(e), m, line);
            } else {
                break;
            }
        }
        return e;
    }
    NodeP primary() {
        int line = cur().line;
        if (match(TT::NUMBER)) return std::make_unique<LiteralNode>(prev().num, line);
        if (match(TT::STRING)) return std::make_unique<LiteralNode>(make_str(prev().text), line);
        if (match(TT::TRUE_)) return std::make_unique<LiteralNode>(Value::boolean(true), line);
        if (match(TT::FALSE_)) return std::make_unique<LiteralNode>(Value::boolean(false), line);
        if (match(TT::CIN)) {
            consume(TT::LPAREN, "Expected '(' after 'cin'");
            NodeP prompt;
            if (!check(TT::RPAREN)) prompt = expression();
            consume(TT::RPAREN, "Expected ')' after 'cin'");
            return std::make_unique<CinNode>(std::move(prompt), line);
        }
        if (match(TT::IDENTIFIER)) return std::make_unique<IdentNode>(prev().text, line);
        if (match(TT::LPAREN)) {
            NodeP e = expression();
            consume(TT::RPAREN, "Expected ')' after grouped expression");
            return e;
        }
        if (match(TT::LBRACK)) {
            auto v = std::make_unique<VectorNode>(line);
            if (!check(TT::RBRACK)) {
                v->elems.push_back(expression());
                while (match(TT::COMMA)) v->elems.push_back(expression());
            }
            consume(TT::RBRACK, "Expected ']' at the end of vector");
            return v;
        }
        if (match(TT::LBRACE)) {
            auto d = std::make_unique<DictNode>(line);
            if (!check(TT::RBRACE)) {
                do {
                    NodeP k = expression();
                    consume(TT::COLON, "Expected ':' after dictionary key");
                    NodeP v = expression();
                    d->pairs.emplace_back(std::move(k), std::move(v));
                } while (match(TT::COMMA));
            }
            consume(TT::RBRACE, "Expected '}' at the end of dictionary literal");
            return d;
        }
        error("Expected expression or value representation");
    }
};

inline std::unique_ptr<ProgramNode> parse_source(const std::string& src) {
    Lexer lx(src);
    Parser ps(lx.tokenize());
    return ps.parse();
}

}  // namespace nix

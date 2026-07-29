import sys
import os
import subprocess
import argparse
import shutil
from typing import List, Dict, Any, Optional, Union

try:
    from lexer import Lexer
    from nix_parser import Parser
    from ast_nodes import (
        ProgramNode, VarAssignNode, FuncDefNode, IfStmtNode, 
        WhileLoopNode, ForLoopNode, ReturnNode, OutNode, 
        BinOpNode, UnaryOpNode, LiteralNode, IdentifierNode, 
        FuncCallNode, CinCallNode, VectorNode, DictionaryNode, SubscriptNode,
        ClassDefNode, GetAttrNode
    )
except ImportError:
    # Fallback dummy classes in case ast_nodes is not available in the environment
    class ASTNode: pass
    class ProgramNode(ASTNode):
        def __init__(self, statements): self.statements = statements
    class VarAssignNode(ASTNode):
        def __init__(self, target, value_expr): self.target, self.value_expr = target, value_expr
    class FuncDefNode(ASTNode):
        def __init__(self, name, params, body_block): self.name, self.params, self.body_block = name, params, body_block
    class IfStmtNode(ASTNode):
        def __init__(self, condition, then_block, else_block=None): self.condition, self.then_block, self.else_block = condition, then_block, else_block
    class WhileLoopNode(ASTNode):
        def __init__(self, condition, body_block): self.condition, self.body_block = condition, body_block
    class ForLoopNode(ASTNode):
        def __init__(self, var_name, start_expr, end_expr, step_expr, body_block): self.var_name, self.start_expr, self.end_expr, self.step_expr, self.body_block = var_name, start_expr, end_expr, step_expr, body_block
    class ReturnNode(ASTNode):
        def __init__(self, expr=None): self.expr = expr
    class OutNode(ASTNode):
        def __init__(self, arguments): self.arguments = arguments
    class BinOpNode(ASTNode):
        def __init__(self, left, op, right): self.left, self.op, self.right = left, op, right
    class UnaryOpNode(ASTNode):
        def __init__(self, op, expr): self.op, self.expr = op, expr
    class LiteralNode(ASTNode):
        def __init__(self, value): self.value = value
    class IdentifierNode(ASTNode):
        def __init__(self, name): self.name = name
    class FuncCallNode(ASTNode):
        def __init__(self, name, arguments): self.name, self.arguments = name, arguments
    class CinCallNode(ASTNode):
        def __init__(self, prompt_expr=None): self.prompt_expr = prompt_expr
    class VectorNode(ASTNode):
        def __init__(self, elements): self.elements = elements
    class DictionaryNode(ASTNode):
        def __init__(self, pairs): self.pairs = pairs
    class SubscriptNode(ASTNode):
        def __init__(self, collection, index_expr): self.collection, self.index_expr = collection, index_expr
    class ClassDefNode(ASTNode):
        def __init__(self, name, body_block): self.name, self.body_block = name, body_block
    class GetAttrNode(ASTNode):
        def __init__(self, object_expr, member_name): self.object_expr, self.member_name = object_expr, member_name

CPP_RUNTIME_HEADER = r"""
// ==============================================================================
// NIX PROGRAMMING LANGUAGE - EMBEDDED C++ NATIVE RUNTIME ENVIRONMENT
// ==============================================================================
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <iomanip>

struct NixValue {
    enum Type { NIL, DOUBLE, BOOL, STRING, VECTOR, DICT } type = NIL;
    double numVal = 0.0;
    bool boolVal = false;
    std::string strVal;
    std::vector<NixValue> vecVal;
    std::map<std::string, NixValue> dictVal;

    // Constructors
    NixValue() : type(NIL) {}
    NixValue(double v) : type(DOUBLE), numVal(v) {}
    NixValue(int v) : type(DOUBLE), numVal(static_cast<double>(v)) {}
    NixValue(bool v) : type(BOOL), boolVal(v) {}
    NixValue(const char* v) : type(STRING), strVal(v) {}
    NixValue(const std::string& v) : type(STRING), strVal(v) {}
    NixValue(const std::vector<NixValue>& v) : type(VECTOR), vecVal(v) {}
    NixValue(const std::map<std::string, NixValue>& v) : type(DICT), dictVal(v) {}

    // Truthiness evaluation helper
    bool isTrue() const {
        if (type == BOOL) return boolVal;
        if (type == DOUBLE) return numVal != 0.0;
        if (type == STRING) return !strVal.empty();
        if (type == VECTOR) return !vecVal.empty();
        if (type == DICT) return !dictVal.empty();
        return false;
    }

    // String conversion for console printing
    std::string toString() const {
        if (type == NIL) return "nil";
        if (type == BOOL) return boolVal ? "true" : "false";
        if (type == STRING) return strVal;
        if (type == DOUBLE) {
            std::ostringstream ss;
            if (numVal == static_cast<long long>(numVal)) {
                ss << static_cast<long long>(numVal);
            } else {
                ss << std::setprecision(10) << numVal;
            }
            return ss.str();
        }
        if (type == VECTOR) {
            std::string res = "[";
            for (size_t i = 0; i < vecVal.size(); ++i) {
                if (i > 0) res += ", ";
                res += vecVal[i].toString();
            }
            res += "]";
            return res;
        }
        if (type == DICT) {
            std::string res = "{";
            size_t i = 0;
            for (const auto& pair : dictVal) {
                if (i > 0) res += ", ";
                res += "\"" + pair.first + "\": " + pair.second.toString();
                i++;
            }
            res += "}";
            return res;
        }
        return "";
    }

    NixValue operator+(const NixValue& rhs) const {
        if (type == STRING || rhs.type == STRING) {
            return NixValue(this->toString() + rhs.toString());
        }
        if (type == VECTOR && rhs.type == VECTOR) {
            std::vector<NixValue> combined = vecVal;
            combined.insert(combined.end(), rhs.vecVal.begin(), rhs.vecVal.end());
            return NixValue(combined);
        }
        return NixValue(numVal + rhs.numVal);
    }

    NixValue operator-(const NixValue& rhs) const {
        return NixValue(numVal - rhs.numVal);
    }

    NixValue operator*(const NixValue& rhs) const {
        if (type == STRING && rhs.type == DOUBLE) {
            std::string res = "";
            int count = static_cast<int>(rhs.numVal);
            for (int i = 0; i < count; ++i) res += strVal;
            return NixValue(res);
        }
        return NixValue(numVal * rhs.numVal);
    }

    NixValue operator/(const NixValue& rhs) const {
        if (rhs.numVal == 0.0) {
            throw std::runtime_error("Nix RuntimeError: Division by zero");
        }
        return NixValue(numVal / rhs.numVal);
    }

    NixValue operator%(const NixValue& rhs) const {
        if (static_cast<long long>(rhs.numVal) == 0) {
            throw std::runtime_error("Nix RuntimeError: Modulo by zero");
        }
        return NixValue(static_cast<double>(static_cast<long long>(numVal) % static_cast<long long>(rhs.numVal)));
    }

    NixValue operator-() const {
        if (type == DOUBLE) return NixValue(-numVal);
        return *this;
    }

    // Comparison Operators
    bool operator==(const NixValue& rhs) const {
        if (type != rhs.type) return false;
        if (type == DOUBLE) return numVal == rhs.numVal;
        if (type == BOOL) return boolVal == rhs.boolVal;
        if (type == STRING) return strVal == rhs.strVal;
        return false;
    }

    bool operator!=(const NixValue& rhs) const { return !(*this == rhs); }

    bool operator<(const NixValue& rhs) const {
        if (type == STRING && rhs.type == STRING) return strVal < rhs.strVal;
        return numVal < rhs.numVal;
    }

    bool operator>(const NixValue& rhs) const {
        if (type == STRING && rhs.type == STRING) return strVal > rhs.strVal;
        return numVal > rhs.numVal;
    }

    bool operator<=(const NixValue& rhs) const {
        return (*this < rhs) || (*this == rhs);
    }

    bool operator>=(const NixValue& rhs) const {
        return (*this > rhs) || (*this == rhs);
    }

    NixValue& operator[](const NixValue& index) {
        if (type == VECTOR) {
            int idx = static_cast<int>(index.numVal);
            if (idx < 0 || idx >= static_cast<int>(vecVal.size())) {
                throw std::runtime_error("Nix RuntimeError: Vector index out of bounds: " + std::to_string(idx));
            }
            return vecVal[idx];
        }
        if (type == DICT) {
            return dictVal[index.toString()];
        }
        throw std::runtime_error("Nix RuntimeError: Subscript assignment on non-collection type");
    }

    NixValue getMember(const std::string& member) const {
        if (type == DICT) {
            auto it = dictVal.find(member);
            if (it != dictVal.end()) return it->second;
        }
        throw std::runtime_error("Nix RuntimeError: Attribute or method '" + member + "' not found");
    }

    void setMember(const std::string& member, const NixValue& val) {
        if (type == DICT) {
            dictVal[member] = val;
            return;
        }
        throw std::runtime_error("Nix RuntimeError: Cannot set attribute on non-object type");
    }

    NixValue getItem(const NixValue& index) const {
        if (type == VECTOR) {
            int idx = static_cast<int>(index.numVal);
            if (idx < 0 || idx >= static_cast<int>(vecVal.size())) {
                throw std::runtime_error("Nix RuntimeError: Vector index out of bounds: " + std::to_string(idx));
            }
            return vecVal[idx];
        }
        if (type == DICT) {
            auto it = dictVal.find(index.toString());
            if (it == dictVal.end()) {
                throw std::runtime_error("Nix RuntimeError: Key not found in dictionary: " + index.toString());
            }
            return it->second;
        }
        throw std::runtime_error("Nix RuntimeError: Subscript access failed on non-collection type");
    }
};

// Stream insertion operator for NixValue
inline std::ostream& operator<<(std::ostream& os, const NixValue& val) {
    os << val.toString();
    return os;
}

inline void nix_out_single(const NixValue& val) {
    std::cout << val.toString();
}

template <typename... Args>
inline void nix_out(Args... args) {
    size_t count = 0;
    size_t total = sizeof...(args);
    auto print_item = [&](const NixValue& item) {
        std::cout << item.toString();
        count++;
        if (count < total) std::cout << " ";
    };
    (print_item(args), ...);
    std::cout << std::endl;
}

inline NixValue nix_cin(const NixValue& prompt = NixValue("")) {
    if (!prompt.toString().empty()) {
        std::cout << prompt.toString();
        std::cout.flush();
    }
    std::string inputStr;
    if (std::getline(std::cin, inputStr)) {
        if (!inputStr.empty() && inputStr.back() == '\r') {
            inputStr.pop_back();
        }
        // Attempt numeric conversion for input
        try {
            size_t idx;
            double d = std::stod(inputStr, &idx);
            if (idx == inputStr.length()) {
                return NixValue(d);
            }
        } catch (...) {}
        return NixValue(inputStr);
    }
    return NixValue("");
}

// Built-in Native Utilities and Math Functions
inline NixValue nix_len(const NixValue& v) {
    if (v.type == NixValue::VECTOR) return NixValue(static_cast<double>(v.vecVal.size()));
    if (v.type == NixValue::STRING) return NixValue(static_cast<double>(v.strVal.length()));
    if (v.type == NixValue::DICT) return NixValue(static_cast<double>(v.dictVal.size()));
    return NixValue(0.0);
}

inline NixValue nix_math_sin(const NixValue& v) { return NixValue(std::sin(v.numVal)); }
inline NixValue nix_math_cos(const NixValue& v) { return NixValue(std::cos(v.numVal)); }
inline NixValue nix_math_sqrt(const NixValue& v) { return NixValue(std::sqrt(v.numVal)); }
inline NixValue nix_math_fabs(const NixValue& v) { return NixValue(std::abs(v.numVal)); }
inline NixValue nix_math_pow(const NixValue& b, const NixValue& e) { return NixValue(std::pow(b.numVal, e.numVal)); }
"""

class NixToCppTranspiler:
    """
    AST Visitor and Transpiler that translates Nix AST nodes into standard C++ source code.
    Generates C++ functions, type conversions, dynamic bindings, and control structures.
    """

    def __init__(self):
        self.indent_level = 0
        self.declared_functions = set()
        self.function_signatures = []
        self.function_definitions = []
        self.main_body_lines = []
        self.temp_var_counter = 0
        
        # Scopes tracking: Prevents 'undeclared identifier' errors in C++ 
        # by properly re-declaring variables inside localized function/block scopes.
        self.env_scopes: List[set] = [set()] 

    def enter_scope(self):
        self.env_scopes.append(set())
        
    def exit_scope(self):
        if len(self.env_scopes) > 1:
            self.env_scopes.pop()

    def is_var_declared(self, name: str) -> bool:
        """Checks if a variable has already been typed and declared in the current or parent C++ scope."""
        for scope in reversed(self.env_scopes):
            if name in scope:
                return True
        return False

    def declare_var(self, name: str):
        """Registers a variable in the current local scope."""
        self.env_scopes[-1].add(name)

    def get_indent(self) -> str:
        """Returns string padding for current code indentation level."""
        return "    " * self.indent_level

    def generate_temp_var(self, prefix: str = "_tmp") -> str:
        """Generates unique temporary variable identifier for nested expressions."""
        self.temp_var_counter += 1
        return f"{prefix}_{self.temp_var_counter}"

    def transpile(self, program_ast: ProgramNode) -> str:
        """
        Main entry point for transpiling an entire Nix Program AST into a fully runnable C++ file.
        """
        self.indent_level = 1
        self.env_scopes = [set()] # Reset to global scope
        
        # Process top-level statements
        if hasattr(program_ast, 'statements'):
            for stmt in program_ast.statements:
                if isinstance(stmt, FuncDefNode):
                    self.visit_func_def(stmt)
                else:
                    line = self.visit_statement(stmt)
                    if line:
                        self.main_body_lines.append(line)

        # Assemble the final C++ source code string
        cpp_code = [CPP_RUNTIME_HEADER, "\n// Forward Function Declarations"]
        for sig in self.function_signatures:
            cpp_code.append(sig + ";")

        cpp_code.append("\n// User Defined Nix Functions")
        for func_def in self.function_definitions:
            cpp_code.append(func_def)

        cpp_code.append("\n// Main Entry Point")
        cpp_code.append("int main() {")
        cpp_code.append("    try {")

        for line in self.main_body_lines:
            cpp_code.append("        " + line)

        cpp_code.append("    } catch (const std::exception& ex) {")
        cpp_code.append("        std::cerr << ex.what() << std::endl;")
        cpp_code.append("        return 1;")
        cpp_code.append("    }")
        cpp_code.append("    return 0;")
        cpp_code.append("}")

        return "\n".join(cpp_code)

    def visit_statement(self, node: Any) -> str:
        """Routes generic statements to their specific visitor handlers."""
        if node is None:
            return ""

        # If the node is a list of statements (e.g., the contents of a { ... } block)
        if isinstance(node, list):
            lines = [self.visit_statement(stmt) for stmt in node]
            return "\n".join(line for line in lines if line and line.strip())

        # Safely unwrap block nodes which hold statements in an attribute
        if hasattr(node, 'statements') and isinstance(node.statements, list):
            lines = [self.visit_statement(stmt) for stmt in node.statements]
            return "\n".join(line for line in lines if line and line.strip())

        if isinstance(node, OutNode):
            return self.visit_out_node(node)
        elif isinstance(node, VarAssignNode):
            return self.visit_var_assign(node)
        elif isinstance(node, IfStmtNode):
            return self.visit_if_stmt(node)
        elif isinstance(node, WhileLoopNode):
            return self.visit_while_loop(node)
        elif isinstance(node, ForLoopNode):
            return self.visit_for_loop(node)
        elif isinstance(node, FuncDefNode):
            return self.visit_func_def(node)
        elif isinstance(node, ReturnNode):
            return self.visit_return(node)
        elif isinstance(node, ClassDefNode):
            self.visit_class_def(node)
            return ""

        # If it's an isolated expression used as a standalone statement
        expr_code = self.visit_expression(node)
        return f"    {expr_code};" if expr_code else ""

    def visit_class_def(self, node: ClassDefNode) -> None:
        """Transpiles a Nix class into a C++ object factory function returning a dictionary map."""
        self.declared_functions.add(node.name)
        signature = f"NixValue {node.name}()"
        self.function_signatures.append(signature)

        body_code = [f"{signature} {{"]
        body_code.append("    std::map<std::string, NixValue> _instance_fields;")
        
        # FIX: Robustly extract statements from the class body block
        body_node = node.body_block
        stmts = []
        if hasattr(body_node, 'statements'):
            stmts = body_node.statements
        elif isinstance(body_node, list):
            stmts = body_node
        else:
            stmts = [body_node]

        # Iterate through the correctly extracted class body statements
        for stmt in stmts:
            if isinstance(stmt, VarAssignNode):
                val_code = self.visit_expression(stmt.value_expr)
                target_name = stmt.target.name if hasattr(stmt.target, 'name') else str(stmt.target)
                body_code.append(f'    _instance_fields["{target_name}"] = {val_code};')
            elif isinstance(stmt, FuncDefNode):
                # Transpile the method separately and store its reference as a string
                self.visit_func_def(stmt)
                body_code.append(f'    _instance_fields["{stmt.name}"] = NixValue("{stmt.name}");')

        body_code.append("    return NixValue(_instance_fields);")
        body_code.append("}\n")

        self.function_definitions.append("\n".join(body_code))

    def visit_var_assign(self, node: VarAssignNode) -> str:
        """Translates variable assignment / reassignment and property mutation to C++."""
        val_code = self.visit_expression(node.value_expr)
        target = getattr(node, 'target', None) or getattr(node, 'var_node', None)

        # 1. Subscript assignment (e.g., arr[0] = 5)
        if isinstance(target, SubscriptNode):
            coll_code = self.visit_expression(target.collection)
            idx_code = self.visit_expression(target.index_expr)
            return f"    {coll_code}[{idx_code}] = {val_code};"

        # 2. Property assignment (e.g., p.score = 250)
        if isinstance(target, GetAttrNode) or (hasattr(target, 'object_expr') and hasattr(target, 'member_name')):
            obj_expr = getattr(target, 'object_expr', None)
            mem_name = getattr(target, 'member_name', None)
            obj_code = self.visit_expression(obj_expr)
            return f'    {obj_code}.setMember("{mem_name}", {val_code});'

        # 3. Classic variable assignment (e.g., score = 100)
        var_name = target.name if hasattr(target, 'name') else str(target)

        if not self.is_var_declared(var_name):
            self.declare_var(var_name)
            return f"    NixValue {var_name} = {val_code};"
        else:
            return f"    {var_name} = {val_code};"

    def visit_func_def(self, node: FuncDefNode) -> None:
        """Translates Nix function definitions into C++ functions taking NixValue arguments."""
        self.declared_functions.add(node.name)
        params = [f"NixValue {p}" for p in node.params]
        param_str = ", ".join(params)
        
        signature = f"NixValue {node.name}({param_str})"
        self.function_signatures.append(signature)

        # Create isolated variable scope for function parameters
        self.enter_scope()
        for p in node.params:
            self.declare_var(p)

        body_code = []
        body_code.append(f"{signature} {{")

        # Robust block extraction for functions
        body_node = node.body_block
        body_stmts = []
        if hasattr(body_node, 'statements'):
            body_stmts = body_node.statements
        elif isinstance(body_node, list):
            body_stmts = body_node
        else:
            body_stmts = [body_node]
            
        for stmt in body_stmts:
            stmt_str = self.visit_statement(stmt)
            if stmt_str:
                body_code.append(f"    {stmt_str}")

        body_code.append("    return NixValue(); // Default Return")
        body_code.append("}\n")

        self.exit_scope()
        self.function_definitions.append("\n".join(body_code))

    def visit_if_stmt(self, node: IfStmtNode) -> str:
        """Translates IfStmtNode to C++ if/else conditional structure."""
        cond_code = self.visit_expression(node.condition)
        
        then_node = getattr(node, 'then_block', None) or getattr(node, 'then_branch', None) or getattr(node, 'then_body', None)
        else_node = getattr(node, 'else_block', None) or getattr(node, 'else_branch', None) or getattr(node, 'else_body', None)
        
        then_code = self.visit_statement(then_node)
        cpp_code = f"    if (({cond_code}).isTrue()) {{\n{then_code}\n    }}"
        
        if else_node:
            else_code = self.visit_statement(else_node)
            cpp_code += f" else {{\n{else_code}\n    }}"
            
        return cpp_code

    def visit_while_loop(self, node: WhileLoopNode) -> str:
        """Translates WhileLoopNode to C++ while loop structure."""
        cond_code = self.visit_expression(node.condition)
        body_node = getattr(node, 'body_block', None) or getattr(node, 'body', None) or getattr(node, 'statements', None)
        body_code = self.visit_statement(body_node)
        return f"    while (({cond_code}).isTrue()) {{\n{body_code}\n    }}"

    def visit_for_loop(self, node: ForLoopNode) -> str:
        """Translates ForLoopNode (e.g., 'for i * limit { ... }') to C++ for loop."""
        var_name = (
            getattr(node, 'var_name', None) or 
            getattr(node, 'iterator_name', None) or 
            getattr(node, 'var', None) or 
            getattr(node, 'iterator', 'i')
        )
        if hasattr(var_name, 'name'):
            var_name = var_name.name

        limit_node = getattr(node, 'limit_expr', None) or getattr(node, 'end_expr', None) or getattr(node, 'limit', None) or getattr(node, 'count_expr', None)
        limit_code = self.visit_expression(limit_node) if limit_node else "NixValue(0)"

        body_node = getattr(node, 'body_block', None) or getattr(node, 'body', None) or getattr(node, 'statements', None)
        
        # Self-contained scoping for loop
        self.enter_scope()
        self.declare_var(var_name)
        body_code = self.visit_statement(body_node)
        self.exit_scope()

        return (
            f"    for (NixValue {var_name} = 0; "
            f"{var_name} < {limit_code}; "
            f"{var_name} = {var_name} + 1) {{\n"
            f"{body_code}\n"
            f"    }}"
        )

    def visit_return(self, node: ReturnNode) -> str:
        """Translates ReturnNode to C++ return statement."""
        expr_node = getattr(node, 'value_expr', None) or getattr(node, 'value', None) or getattr(node, 'expr', None)
        if expr_node:
            val_code = self.visit_expression(expr_node)
            return f"    return {val_code};"
        return "    return NixValue();"

    def visit_out_node(self, node: OutNode) -> str:
        """Translates OutNode (console output) to nix_out C++ macro function."""
        arg_codes = [self.visit_expression(arg) for arg in node.arguments]
        joined_args = ", ".join(arg_codes)
        return f"nix_out({joined_args});"

    def visit_expression(self, node: Any) -> str:
        """Dispatches expression nodes to generate corresponding C++ rvalues."""
        if node is None:
            return "NixValue()"

        if isinstance(node, LiteralNode):
            return self.visit_literal(node)
        elif isinstance(node, IdentifierNode):
            return node.name
        elif isinstance(node, BinOpNode):
            return self.visit_bin_op(node)
        elif isinstance(node, UnaryOpNode):
            return self.visit_unary_op(node)
        elif isinstance(node, FuncCallNode):
            return self.visit_func_call(node)
        elif isinstance(node, CinCallNode):
            prompt = self.visit_expression(node.prompt_expr) if getattr(node, 'prompt_expr', None) else "NixValue(\"\")"
            return f"nix_cin({prompt})"
        elif isinstance(node, VectorNode):
            return self.visit_vector(node)
        elif isinstance(node, DictionaryNode):
            pairs = None
            for attr in ['elements', 'pairs', 'entries', 'items', 'kv_pairs', 'dict_items']:
                if hasattr(node, attr) and getattr(node, attr) is not None:
                    pairs = getattr(node, attr)
                    break
        
            if pairs is None:
                pairs = next((v for v in node.__dict__.values() if isinstance(v, (list, tuple, dict))), [])

            if isinstance(pairs, dict):
                pairs = list(pairs.items())

            cpp_pairs = []
            for item in pairs:
                key_node, val_node = None, None
                if isinstance(item, (list, tuple)) and len(item) == 2:
                    key_node, val_node = item
                elif hasattr(item, 'key') and hasattr(item, 'value'):
                    key_node, val_node = item.key, item.value
            
                if key_node is not None and val_node is not None:
                    k_code = self.visit_expression(key_node)
                    v_code = self.visit_expression(val_node)
                    cpp_pairs.append(f"{{{k_code}.toString(), {v_code}}}")

            pairs_str = ", ".join(cpp_pairs)
            return f"NixValue(std::map<std::string, NixValue>{{{pairs_str}}})"

        # Reading an attribute/property (e.g. p.score)
        elif isinstance(node, GetAttrNode) or (hasattr(node, 'object_expr') and hasattr(node, 'member_name')):
            obj_expr = getattr(node, 'object_expr', None)
            mem_name = getattr(node, 'member_name', None)
            obj_code = self.visit_expression(obj_expr)
            return f'({obj_code}).getMember("{mem_name}")'

        elif isinstance(node, SubscriptNode):
            return self.visit_subscript(node)
        elif isinstance(node, (int, float, str, bool)):
            return self.visit_literal(LiteralNode(node))

        return "NixValue()"

    def visit_literal(self, node: LiteralNode) -> str:
        """Encapsulates literal primitive values into NixValue constructors."""
        val = node.value
        if isinstance(val, bool):
            return f"NixValue({'true' if val else 'false'})"
        elif isinstance(val, (int, float)):
            return f"NixValue({val})"
        elif isinstance(val, str):
            escaped = val.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')
            return f'NixValue("{escaped}")'
        return "NixValue()"

    def visit_bin_op(self, node: BinOpNode) -> str:
        """Translates binary expression operations including arithmetic, logical and comparisons."""
        left_code = self.visit_expression(node.left)
        right_code = self.visit_expression(node.right)
        op = node.op

        if op in ['+', '-', '*', '/', '%']:
            return f"({left_code} {op} {right_code})"
        elif op == '==':
            return f"NixValue({left_code} == {right_code})"
        elif op == '!=':
            return f"NixValue({left_code} != {right_code})"
        elif op in ['<', '>', '<=', '>=']:
            return f"NixValue({left_code} {op} {right_code})"
        elif op in ['and', '&&']:
            return f"NixValue(({left_code}).isTrue() && ({right_code}).isTrue())"
        elif op in ['or', '||']:
            return f"NixValue(({left_code}).isTrue() || ({right_code}).isTrue())"

        return f"({left_code} + {right_code})"

    def visit_unary_op(self, node: UnaryOpNode) -> str:
        """Translates unary expression operations (-x, !x)."""
        expr_code = self.visit_expression(node.expr)
        if node.op == '-':
            return f"(-{expr_code})"
        elif node.op in ['not', '!']:
            return f"NixValue(!({expr_code}).isTrue())"
        return expr_code

    def visit_func_call(self, node: FuncCallNode) -> str:
        """Translates function calls into native C++ calls, resolving method calls and built-ins."""
        arg_codes = [self.visit_expression(arg) for arg in node.arguments]
        args_str = ", ".join(arg_codes)
        
        # Robustly extract function name whether it's a string, Identifier, or Method Call
        if hasattr(node.name, 'name'):
            func_name = node.name.name
        elif isinstance(node.name, GetAttrNode) or (hasattr(node.name, 'object_expr') and hasattr(node.name, 'member_name')):
            # If the user tries to call a method like 'p.greet()', we fallback to global function 'greet(args)'
            func_name = getattr(node.name, 'member_name', None)
        else:
            func_name = str(node.name)

        # Map to Built-in native function mappings
        if func_name == "cin":
            prompt = arg_codes[0] if arg_codes else "NixValue(\"\")"
            return f"nix_cin({prompt})"
        elif func_name == "len":
            target = arg_codes[0] if arg_codes else "NixValue()"
            return f"nix_len({target})"
        elif func_name == "math_sin":
            return f"nix_math_sin({args_str})"
        elif func_name == "math_cos":
            return f"nix_math_cos({args_str})"
        elif func_name == "math_sqrt":
            return f"nix_math_sqrt({args_str})"
        elif func_name == "math_pow":
            return f"nix_math_pow({args_str})"
        elif func_name == "math_abs":
            return f"nix_math_fabs({args_str})"

        # Clean string if it accidentally contains dot notations
        if "." in func_name:
            func_name = func_name.split(".")[-1]

        return f"{func_name}({args_str})"

    def visit_vector(self, node: VectorNode) -> str:
        """Translates Nix vector/array literals into C++ vector initializers."""
        elem_codes = [self.visit_expression(elem) for elem in node.elements]
        joined_elems = ", ".join(elem_codes)
        return f"NixValue(std::vector<NixValue>{{{joined_elems}}})"

    def visit_dictionary(self, node: DictionaryNode) -> str:
        """Translates Nix dictionary literals into C++ map initializers."""
        pair_codes = []
        for key_node, val_node in node.pairs:
            key_str = key_node.value if isinstance(key_node, LiteralNode) else str(key_node)
            val_code = self.visit_expression(val_node)
            pair_codes.append(f'{{"{key_str}", {val_code}}}')
        joined_pairs = ", ".join(pair_codes)
        return f"NixValue(std::map<std::string, NixValue>{{{joined_pairs}}})"

    def visit_subscript(self, node: SubscriptNode) -> str:
        """Translates subscript access expressions: arr[0] or dict["key"]."""
        coll_code = self.visit_expression(node.collection)
        idx_code = self.visit_expression(node.index_expr)
        return f"({coll_code}).getItem({idx_code})"

class NixCompilerDriver:
    """
    Compiler Pipeline Driver that handles code generation, native C++ compiler invocation
    (G++ or Clang++), binary linking, and executable generation.
    """

    def __init__(self, compiler_binary: Optional[str] = None):
        self.compiler_binary = compiler_binary or self.detect_cpp_compiler()

    @staticmethod
    def detect_cpp_compiler() -> str:
        """Locates available C++ compilers on host OS (g++, clang++, or msvc cl)."""
        for compiler in ['g++', 'clang++', 'c++']:
            if shutil.which(compiler):
                return compiler
        raise RuntimeError("Environment Error: No native C++ compiler (g++ or clang++) found on standard PATH.")

    def compile_source(
        self, 
        nix_code: str, 
        output_binary: str = "program_exec", 
        opt_level: str = "-O2",
        keep_cpp: bool = False
    ) -> str:
        """
        Parses source Nix script, transpiles to C++, invokes C++ compiler, and links output binary.
        """
        # Step 1: Lexing and Parsing
        lexer = Lexer(nix_code)
        tokens = lexer.tokenize()
        parser = Parser(tokens)
        ast = parser.parse()

        # Step 2: Code Generation
        transpiler = NixToCppTranspiler()
        cpp_source = transpiler.transpile(ast)

        cpp_filename = output_binary + ".cpp"
        with open(cpp_filename, 'w', encoding='utf-8') as f:
            f.write(cpp_source)

        # Step 3: Native Compilation Execution
        cmd = [self.compiler_binary, opt_level, "-std=c++17", cpp_filename, "-o", output_binary]
        
        print(f"[Nix Compiler] Compiling C++ source using '{self.compiler_binary}'...")
        result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

        if result.returncode != 0:
            print(f"[Nix Compilation Error]:\n{result.stderr}")
            raise RuntimeError("Native C++ compilation failed.")

        print(f"[Nix Compiler] Binary successfully produced: '{output_binary}'")

        if not keep_cpp and os.path.exists(cpp_filename):
            os.remove(cpp_filename)

        return output_binary


def main():
    """Command Line Interface for the Nix Native Compiler Toolchain."""
    parser = argparse.ArgumentParser(description="Nix Native Language Compiler (Transpiles Nix to C++ Native Binaries)")
    parser.add_argument("source", help="Path to input Nix source file (.nx or .nix)")
    parser.add_argument("-o", "--output", default="a.out", help="Output executable binary filepath")
    parser.add_argument("--emit-cpp", action="store_true", help="Keep generated intermediate .cpp file")
    parser.add_argument("-O", "--optimization", default="-O2", help="C++ Optimization level (-O0, -O2, -O3)")
    parser.add_argument("-r", "--run", action="store_true", help="Execute binary immediately after successful build")

    args = parser.parse_args()

    if not os.path.exists(args.source):
        print(f"Error: Source file '{args.source}' does not exist.")
        sys.exit(1)

    with open(args.source, 'r', encoding='utf-8') as f:
        source_code = f.read()

    driver = NixCompilerDriver()
    try:
        binary_path = driver.compile_source(
            nix_code=source_code,
            output_binary=args.output,
            opt_level=args.optimization,
            keep_cpp=args.emit_cpp
        )

        if args.run:
            print(f"[Nix Compiler] Executing generated binary '{binary_path}'...\n" + "="*50)
            subprocess.run([f"./{binary_path}" if not binary_path.startswith("/") else binary_path])

    except Exception as err:
        print(f"Compiler Pipeline Error: {err}")
        sys.exit(1)

def compile_file(input_file, output):
    if not os.path.exists(input_file):
        raise RuntimeError(f"File not found: {input_file}")

    with open(input_file, 'r', encoding='utf-8') as f:
        source_code = f.read()

    driver = NixCompilerDriver()

    return driver.compile_source(
        nix_code=source_code,
        output_binary=output
    )
    
    


if __name__ == '__main__':
    main()
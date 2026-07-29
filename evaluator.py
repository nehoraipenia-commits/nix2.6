from ast_nodes import (
    ProgramNode, VarAssignNode, FuncDefNode, IfStmtNode, 
    WhileLoopNode, ForLoopNode, ReturnNode, BinOpNode, 
    UnaryOpNode, LiteralNode, IdentifierNode, FuncCallNode,
    SubscriptNode, VectorNode, DictionaryNode, ClassDefNode, GetAttrNode
)
from environment import Environment, ReturnSignal, NixRuntimeError, NixInstance


class Evaluator:
    """
    Core Evaluation Engine for the Nix Programming Language.
    Traverses the Abstract Syntax Tree (AST) and executes its nodes
    against a given lexical Environment.
    """
    def __init__(self, global_env=None):
        self.global_env = global_env if global_env is not None else Environment()
        self.builtin_handlers = {}

    def register_builtin(self, node_type, handler_func):
        """Allows external modules to register evaluators for built-in operations."""
        self.builtin_handlers[node_type] = handler_func

    def evaluate(self, node, env):
        """
        Main dispatch method. Dynamically routes any AST node to its 
        corresponding evaluation method.
        """
        if node is None:
            return None

        node_type = type(node)
        
        # Check if an external evaluator has registered a handler for this node type
        if node_type in self.builtin_handlers:
            return self.builtin_handlers[node_type](node, env, self)

        # Core AST evaluation routing
        if node_type is ProgramNode:
            return self.eval_program(node, env)
        elif node_type is VarAssignNode:
            return self.eval_var_assign(node, env)
        elif node_type is GetAttrNode:
            return self.eval_get_attr(node, env)
        elif node_type is ClassDefNode:         
            return self.eval_class_def(node, env)
        elif node_type is FuncDefNode:
            return self.eval_func_def(node, env)
        elif node_type is IfStmtNode:
            return self.eval_if_stmt(node, env)
        elif node_type is WhileLoopNode:
            return self.eval_while_loop(node, env)
        elif node_type is ForLoopNode:
            return self.eval_for_loop(node, env)
        elif node_type is ReturnNode:
            return self.eval_return(node, env)
        elif node_type is BinOpNode:
            return self.eval_bin_op(node, env)
        elif node_type is UnaryOpNode:
            return self.eval_unary_op(node, env)
        elif node_type is LiteralNode:
            return self.eval_literal(node, env)
        elif node_type is IdentifierNode:
            return self.eval_identifier(node, env)
        elif node_type is FuncCallNode:
            return self.eval_func_call(node, env)

        raise NixRuntimeError(f"Unhandled AST Node type: '{node_type.__name__}'", node)

    def eval_program(self, node, env):
        """Executes statements in sequence."""
        result = None
        for stmt in node.statements:
            result = self.evaluate(stmt, env)
        return result

    def eval_var_assign(self, node, env):
        """
        Handles variable assignment (`x = 10`), subscript assignment (`user["score"] = 100`),
        and property assignment (`p.score = 10`).
        """
        value = self.evaluate(node.value_expr, env)

        # 1. Variable simple : x = 10
        if isinstance(node.target, IdentifierNode):
            if env.is_globally_defined(node.target.name):
                env.assign(node.target.name, value, node)
            else:
                env.define(node.target.name, value)
            return value

        # 2. Assignation de propriété d'objet : p.score = 10
        elif isinstance(node.target, GetAttrNode):
            obj = self.evaluate(node.target.object_expr, env)
            if not isinstance(obj, NixInstance):
                raise NixRuntimeError("Cannot assign property to a non-object", node)
            obj.fields[node.target.member_name] = value
            return value

        # 3. Assignation par index/clé : user["score"] = 100 ou vec[0] = 5
        elif isinstance(node.target, SubscriptNode):
            collection = self.evaluate(node.target.collection, env)
            index = self.evaluate(node.target.index_expr, env)

            if isinstance(collection, dict):
                collection[index] = value
            elif isinstance(collection, list):
                if not isinstance(index, int):
                    raise NixRuntimeError(
                        f"Vector subscript index must be an integer, got '{type(index).__name__}'", 
                        node.target.index_expr
                    )
                try:
                    collection[index] = value
                except IndexError:
                    raise NixRuntimeError(f"Vector index out of bounds: {index}", node)
            else:
                raise NixRuntimeError(
                    f"Subscript assignment target must be a Vector or Dictionary, got '{type(collection).__name__}'", 
                    node.target.collection
                )
            return value

        # 4. Fallback pour nom brut de variable
        elif isinstance(node.target, str):
            if env.is_globally_defined(node.target):
                env.assign(node.target, value, node)
            else:
                env.define(node.target, value)
            return value

        raise NixRuntimeError("Invalid assignment target", node)

    def eval_func_def(self, node, env):
        """Registers a function definition into the current environment."""
        env.define(node.name, node)
        return None

    def eval_if_stmt(self, node, env):
        """Evaluates conditional branching."""
        cond_val = self.evaluate(node.condition, env)
        if cond_val:
            return self.evaluate(node.then_branch, env)
        elif node.else_branch:
            return self.evaluate(node.else_branch, env)
        return None

    def eval_while_loop(self, node, env):
        """Executes a loop body repeatedly as long as the condition evaluates to true."""
        result = None
        while self.evaluate(node.condition, env):
            result = self.evaluate(node.body_block, env)
        return result

    #def eval_for_loop(self, node, env):
        #"""Executes a ranged for loop (for i = start to end step)."""
        #start = self.evaluate(node.start_expr, env)
        #end = self.evaluate(node.end_expr, env)
        #step = self.evaluate(node.step_expr, env) if node.step_expr else 1

        #if not all(isinstance(v, (int, float)) for v in [start, end, step]):
            #raise NixRuntimeError("For loop boundaries and step must evaluate to numeric values", node)

        #local_env = Environment(env)
        #result = None
        
        """current = start
        if step > 0:
            while current <= end:
                local_env.define(node.var_name, current)
                result = self.evaluate(node.body_block, local_env)
                current += step
        else:
            while current >= end:
                local_env.define(node.var_name, current)
                result = self.evaluate(node.body_block, local_env)
                current += step

        return result"""

    def eval_for_loop(self, node, env):
        """Executes Nix's custom for loop (for iterator * limit)."""
        limit = self.evaluate(node.limit_expr, env)

        if not isinstance(limit, (int, float)):
            raise NixRuntimeError("For loop limit must evaluate to a numeric value", node)

        local_env = Environment(env)
        result = None
        
        for i in range(int(limit)):
            local_env.define(node.iterator_name, i)
            result = self.evaluate(node.body_block, local_env)

        return result

    def eval_return(self, node, env):
        """Signals a return value interruption from within a function block."""
        value = self.evaluate(node.expr, env) if node.expr else None
        raise ReturnSignal(value)

    def eval_bin_op(self, node, env):
        """Evaluates binary expressions (+, -, *, /, %, ==, !=, <, >, <=, >=)."""
        left = self.evaluate(node.left, env)
        right = self.evaluate(node.right, env)

        op = node.op
        if op == '+':
            return left + right
        elif op == '-':
            return left - right
        elif op == '*':
            return left * right
        elif op == '/':
            if right == 0:
                raise NixRuntimeError("Division by zero", node)
            return left / right
        elif op == '%':
            return left % right
        elif op == '==':
            return left == right
        elif op == '!=':
            return left != right
        elif op == '<':
            return left < right
        elif op == '>':
            return left > right
        elif op == '<=':
            return left <= right
        elif op == '>=':
            return left >= right

        raise NixRuntimeError(f"Unsupported binary operator: '{op}'", node)

    def eval_unary_op(self, node, env):
        """Evaluates unary expressions (-x)."""
        value = self.evaluate(node.expr, env)
        if node.op == '-':
            try:
                return -value
            except TypeError:
                raise NixRuntimeError("Unary minus operator requires a numeric type", node)
        return value

    def eval_literal(self, node, env):
        """Returns the raw literal value directly."""
        return node.value

    def eval_identifier(self, node, env):
        """Looks up a variable symbol's current value in active scopes."""
        return env.lookup(node.name, node)

    def eval_get_attr(self, node, env):
        """
        Permet de lire la propriété d'un objet Nix ou d'un objet Python natif (ex: `p.score` ou `root.title`).
        """
        obj = self.evaluate(node.object_expr, env)

        # 1. Instance de classe Nix
        if isinstance(obj, NixInstance):
            if node.member_name not in obj.fields:
                raise NixRuntimeError(f"Property '{node.member_name}' does not exist on this object", node)
            return obj.fields[node.member_name]

        # 2. Objet Python natif (ex: instance Tkinter root)
        if hasattr(obj, node.member_name):
            return getattr(obj, node.member_name)

        raise NixRuntimeError(f"Cannot read property '{node.member_name}' on object of type '{type(obj).__name__}'", node)

    def eval_func_call(self, node, env):
        """Handles function/method execution and class instantiation."""
        # 1. Résolution du symbole (cible simple ou expression)
        if isinstance(node.target, (IdentifierNode, str)):
            name = node.target.name if isinstance(node.target, IdentifierNode) else node.target
            func = env.lookup(name, node)
        else:
            func = self.evaluate(node.target, env)

        # 2. Instanciation de classe Nix
        if isinstance(func, ClassDefNode):
            instance_env = Environment(self.global_env)
            self.evaluate(func.body_block, instance_env)
            return NixInstance(func, instance_env.variables)

        # 3. Validation de la fonction Nix
        if not isinstance(func, FuncDefNode):
            raise NixRuntimeError(f"Target '{node.target}' is not callable", node)

        if len(node.arguments) != len(func.params):
            raise NixRuntimeError(
                f"Function expects {len(func.params)} arguments, "
                f"but received {len(node.arguments)}", node
            )

        # 4. Exécution de la fonction Nix
        local_env = Environment(self.global_env)
        for param, arg in zip(func.params, node.arguments):
            val = self.evaluate(arg, env)
            local_env.define(param, val)

        try:
            self.evaluate(func.body_block, local_env)
        except ReturnSignal as signal:
            return signal.value
        return None

    def eval_class_def(self, node, env):
        """Enregistre la définition d'une classe dans l'environnement."""
        env.define(node.name, node)
        return None

    def eval_func_call(self, node, env):
        """Handles user function execution with scope isolation and return captures."""
        func = env.lookup(node.name, node)

        if isinstance(func, ClassDefNode):
            # 1. On crée un environnement propre à cet objet
            instance_env = Environment(self.global_env)
            # 2. On exécute le contenu de la classe pour initialiser ses variables
            self.evaluate(func.body_block, instance_env)
            # 3. On retourne une nouvelle instance contenant ces variables
            return NixInstance(func, instance_env.variables)

        if not isinstance(func, FuncDefNode):
            raise NixRuntimeError(f"'{node.name}' is not callable", node)

        if len(node.arguments) != len(func.params):
            raise NixRuntimeError(
                f"Function '{node.name}' expects {len(func.params)} arguments, "
                f"but received {len(node.arguments)}", node
            )

        local_env = Environment(self.global_env)
        for param, arg in zip(func.params, node.arguments):
            val = self.evaluate(arg, env)
            local_env.define(param, val)

        try:
            self.evaluate(func.body_block, local_env)
        except ReturnSignal as signal:
            return signal.value
        return None

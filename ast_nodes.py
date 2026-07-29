class ASTNode:
    """
    Base class for all Abstract Syntax Tree (AST) nodes in the Nix language.
    Every element of the parsed code will inherit from this class.
    """
    pass


class ProgramNode(ASTNode):
    """
    Represents the top-level block of a Nix script or module, 
    containing a sequence of executable statements.
    """
    def __init__(self, statements):
        self.statements = statements

    def __repr__(self):
        return f"ProgramNode(statements={self.statements})"

class UsingNode(ASTNode):
    """
    Représente une directive d'importation en Nix (using <nom_module>).
    """
    def __init__(self, library_name):
        self.library_name = library_name

    def __repr__(self):
        return f"UsingNode(library='{self.library_name}')"
# ==============================================================================
# STATEMENT NODES
# ==============================================================================

class VarAssignNode(ASTNode):
    """
    Represents a variable assignment statement (e.g., x = 10 or names[0] = "nix").
    Supports assignment to both simple identifiers and collection elements.
    """
    def __init__(self, target, value_expr):
        self.target = target          # Can be an IdentifierNode or a SubscriptNode
        self.value_expr = value_expr  # Any evaluable expression

    def __repr__(self):
        return f"VarAssignNode(target={self.target}, value={self.value_expr})"


class FuncDefNode(ASTNode):
    """
    Represents a function definition block in Nix.
    Syntax: fct name(param1, param2) { body }
    """
    def __init__(self, name, params, body_block):
        self.name = name              # Name of the function (string)
        self.params = params          # List of parameter names (list of strings)
        self.body_block = body_block  # ProgramNode representing the function body

    def __repr__(self):
        return f"FuncDefNode(name='{self.name}', params={self.params}, body={self.body_block})"


class IfStmtNode(ASTNode):
    """
    Represents a conditional control flow statement.
    Syntax: if condition { then_branch } else { else_branch }
    """
    def __init__(self, condition, then_branch, else_branch=None):
        self.condition = condition    # Condition expression evaluating to a boolean
        self.then_branch = then_branch # ProgramNode executed if true
        self.else_branch = else_branch # ProgramNode or IfStmtNode executed if false

    def __repr__(self):
        return f"IfStmtNode(condition={self.condition}, then={self.then_branch}, else={self.else_branch})"


class WhileLoopNode(ASTNode):
    """
    Represents a standard while loop.
    Syntax: while condition { body }
    """
    def __init__(self, condition, body_block):
        self.condition = condition    # Condition expression
        self.body_block = body_block  # ProgramNode to execute iteratively

    def __repr__(self):
        return f"WhileLoopNode(condition={self.condition}, body={self.body_block})"


class ForLoopNode(ASTNode):
    """
    Represents Nix's custom loop syntax.
    Syntax: for iterator * limit_expr { body }
    Iterates 'iterator' from 0 to 'limit_expr - 1'.
    """
    def __init__(self, iterator_name, limit_expr, body_block):
        self.iterator_name = iterator_name # Iterator variable name (string)
        self.limit_expr = limit_expr       # Expression yielding the upper limit
        self.body_block = body_block       # ProgramNode to execute in loop

    def __repr__(self):
        return f"ForLoopNode(iterator='{self.iterator_name}', limit={self.limit_expr}, body={self.body_block})"


class UsingNode(ASTNode):
    """
    Represents an import directive in Nix.
    Syntax: using <library_name>
    Loads external modular `.nx` source files into the runtime.
    """
    def __init__(self, library_name):
        self.library_name = library_name # Name of the target library module (string)

    def __repr__(self):
        return f"UsingNode(library='{self.library_name}')"


class ReturnNode(ASTNode):
    """
    Represents a function return statement.
    Syntax: return expression
    """
    def __init__(self, expr=None):
        self.expr = expr              # The return expression node (optional)

    def __repr__(self):
        return f"ReturnNode(expr={self.expr})"


class OutNode(ASTNode):
    """
    Represents Nix's output printing statement.
    Syntax: out(expr1, expr2, ...)
    """
    def __init__(self, arguments):
        self.arguments = arguments    # List of expressions to evaluate and print

    def __repr__(self):
        return f"OutNode(arguments={self.arguments})"


# ==============================================================================
# EXPRESSION NODES
# ==============================================================================

class BinOpNode(ASTNode):
    """
    Represents binary operations (+, -, *, /, %, comparisons, logical, etc.).
    """
    def __init__(self, left, op, right):
        self.left = left              # Left operand node
        self.op = op                  # Operator symbol (string, e.g., '+', '==')
        self.right = right            # Right operand node

    def __repr__(self):
        return f"BinOpNode(left={self.left}, op='{self.op}', right={self.right})"


class UnaryOpNode(ASTNode):
    """
    Represents unary operations (e.g., negative sign -x).
    """
    def __init__(self, op, expr):
        self.op = op                  # Unary operator symbol (string, e.g., '-')
        self.expr = expr              # Operand expression node

    def __repr__(self):
        return f"UnaryOpNode(op='{self.op}', expr={self.expr})"


class LiteralNode(ASTNode):
    """
    Represents constant literal values (Numbers, Strings, Booleans like true/false).
    """
    def __init__(self, value):
        self.value = value            # Python-native equivalent value (int, float, str, bool)

    def __repr__(self):
        return f"LiteralNode(value={repr(self.value)})"


class IdentifierNode(ASTNode):
    """
    Represents a variable reference.
    """
    def __init__(self, name):
        self.name = name              # Variable name (string)

    def __repr__(self):
        return f"IdentifierNode(name='{self.name}')"


class FuncCallNode(ASTNode):
    """
    Represents a function or method invocation statement or expression.
    Syntax: func_name(arg1, arg2) or object.method(arg1, arg2)
    """
    def __init__(self, target, arguments):
        self.target = target          # IdentifierNode, GetAttrNode, or string
        self.arguments = arguments    # Evaluated expressions passed as arguments

    @property
    def name(self):
        """Backward compatibility for parts of code expecting node.name string."""
        if isinstance(self.target, IdentifierNode):
            return self.target.name
        elif isinstance(self.target, str):
            return self.target
        return str(self.target)

    def __repr__(self):
        return f"FuncCallNode(target={self.target}, arguments={self.arguments})"


class CinCallNode(ASTNode):
    """
    Represents Nix's native interactive console input call.
    Syntax: cin() or cin("Prompt text")
    """
    def __init__(self, prompt_expr=None):
        self.prompt_expr = prompt_expr  # Permet de stocker le message s'il y en a un !

    def __repr__(self):
        return f"CinCallNode(prompt={self.prompt_expr})"


class VectorNode(ASTNode):
    """
    Represents a sequential sequence list (similar to C++ std::vector or Python lists).
    Syntax: [elem1, elem2, elem3]
    """
    def __init__(self, elements):
        self.elements = elements      # List of dynamic expressions inside the list

    def __repr__(self):
        return f"VectorNode(elements={self.elements})"


class DictionaryNode(ASTNode):
    """
    Represents an associative dictionary map mapping keys to values.
    Syntax: {key1: val1, key2: val2}
    """
    def __init__(self, key_value_pairs):
        self.key_value_pairs = key_value_pairs  # Dictionary mapping keys (ASTNodes) to values (ASTNodes)

    def __repr__(self):
        return f"DictionaryNode(pairs={self.key_value_pairs})"


class SubscriptNode(ASTNode):
    """
    Represents indexing lookup syntax on collections like vectors or dictionaries.
    Syntax: collection[index]
    """
    def __init__(self, collection, index_expr):
        self.collection = collection  # The target collection object expression
        self.index_expr = index_expr  # Expression resulting in index/key value

    def __repr__(self):
        return f"SubscriptNode(collection={self.collection}, index={self.index_expr})"


class ClassDefNode(ASTNode):
    """
    Représente une définition de classe en Nix.
    Syntaxe : class Nom { ... }
    """
    def __init__(self, name, body_block):
        self.name = name              # Nom de la classe (ex: "Personne")
        self.body_block = body_block  # Bloc de code à l'intérieur des crochets { }

    def __repr__(self):
        return f"ClassDefNode(name='{self.name}', body={self.body_block})"


class GetAttrNode(ASTNode):
    """
    Représente l'accès à une propriété d'un objet : objet.propriete
    """
    def __init__(self, object_expr, member_name):
        self.object_expr = object_expr  # L'objet (ex: IdentifierNode 'p')
        self.member_name = member_name  # Le nom de la propriété (ex: "score")

    def __repr__(self):
        return f"GetAttrNode(object={self.object_expr}, member='{self.member_name}')"

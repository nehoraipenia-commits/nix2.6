class NixRuntimeError(Exception):
    """
    Custom exception raised when a runtime error occurs during 
    the execution of a Nix program.
    """
    def __init__(self, message, node=None):
        super().__init__(message)
        self.message = message
        self.node = node

    def __str__(self):
        if self.node:
            return f"Nix Runtime Error: {self.message} (associated node: {type(self.node).__name__})"
        return f"Nix Runtime Error: {self.message}"


class ReturnSignal(Exception):
    """
    Exception used as a control flow signal to immediately propagate 
    returned values back up the call stack to the calling function.
    """
    def __init__(self, value):
        self.value = value


class Environment:
    """
    Manages variables, function bindings, and scope hierarchies in Nix.
    Supports local block scoping and fallback resolution to outer/global parent scopes.
    """
    def __init__(self, parent=None):
        self.variables = {}
        self.parent = parent  # Reference to the enclosing outer scope (if any)

    def define(self, name, value):
        """
        Declares and binds a variable in the current local scope.
        Redefines it if it already exists in this specific scope.
        """
        self.variables[name] = value

    def lookup(self, name, node=None):
        """
        Searches for a variable name starting from the current scope 
        and traversing upwards through parent scopes.
        Raises a NixRuntimeError if the variable is undeclared.
        """
        if name in self.variables:
            return self.variables[name]
        if self.parent:
            return self.parent.lookup(name, node)
        raise NixRuntimeError(f"Undefined variable or symbol reference: '{name}'", node)

    def assign(self, name, value, node=None):
        """
        Updates the value of an existing variable.
        Traverses upward to find where the variable was defined, 
        updating it in-place. If not found anywhere, raises an error.
        """
        if name in self.variables:
            self.variables[name] = value
            return
        if self.parent:
            self.parent.assign(name, value, node)
            return
        raise NixRuntimeError(f"Cannot assign to undefined variable: '{name}'", node)

    def is_globally_defined(self, name):
        """
        Checks if a variable exists anywhere in the accessible environment chain.
        """
        if name in self.variables:
            return True
        if self.parent:
            return self.parent.is_globally_defined(name)
        return False

class NixInstance:
    """
    Représente un objet (instance) créé à partir d'une classe Nix.
    """
    def __init__(self, class_node, fields):
        self.class_node = class_node  # La classe d'origine
        self.fields = fields          # Dictionnaire des variables de l'objet (ex: {"score": 0})

    def __repr__(self):
        return f"<{self.class_node.name} instance>"

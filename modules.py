import os
import importlib
import importlib.util
import importlib.machinery

# Import required AST nodes and core components
from ast_nodes import UsingNode, FuncDefNode, IdentifierNode, ClassDefNode
from lexer import Lexer
from nix_parser import Parser
from environment import NixRuntimeError
from evaluator import Evaluator

# Dynamic set to prevent circular imports or reloading modules
LOADED_MODULES = set()

# ==============================================================================
# MONKEY-PATCHING EVALUATOR FOR NATIVE CALLABLES
# ==============================================================================
# The core Nix Evaluator strictly expects FuncDefNode objects inside function 
# calls. To allow native Python functions (such as math.sqrt or random.randint) 
# to be executed directly inside Nix, we patch Evaluator.eval_func_call dynamically.

_original_eval_func_call = Evaluator.eval_func_call

def _nix_custom_eval_func_call(self, node, env):
    """
    Custom wrapper around function evaluation to support direct execution of 
    native Python functions and methods inside Nix code.
    """
    # Résolution de la cible (nom simple ou expression complex ex: root.title)
    if isinstance(node.target, IdentifierNode):
        try:
            func = env.lookup(node.target.name, node)
        except NixRuntimeError:
            return _original_eval_func_call(self, node, env)
    elif isinstance(node.target, str):
        try:
            func = env.lookup(node.target, node)
        except NixRuntimeError:
            return _original_eval_func_call(self, node, env)
    else:
        func = self.evaluate(node.target, env)

    # Si c'est une fonction ou méthode Python native callable
    if callable(func) and not isinstance(func, (FuncDefNode, ClassDefNode)):
        evaluated_args = [self.evaluate(arg, env) for arg in node.arguments]
        try:
            return func(*evaluated_args)
        except Exception as e:
            raise NixRuntimeError(f"Native execution error: {e}", node)
            
    return _original_eval_func_call(self, node, env)

# Apply the patch to the Evaluator class at load-time
Evaluator.eval_func_call = _nix_custom_eval_func_call


# ==============================================================================
# MODULE LOADER FUNCTIONS
# ==============================================================================

def eval_using(node, env, evaluator):
    """
    Handles evaluation of the 'using <library_name>' statement.
    Dynamically loads local modules (.nx, .py, or platform-specific compiled binaries 
    such as .so or .pyd) or falls back to Python system standard libraries.
    """
    module_name = node.library_name
    
    # 1. Check for local native modules in the 'modules/' folder
    # importlib.machinery.EXTENSION_SUFFIXES resolves platform-appropriate binary extensions (.so, .pyd, etc.)
    supported_extensions = [".py"] + importlib.machinery.EXTENSION_SUFFIXES
    native_module_path = None
    
    for ext in supported_extensions:
        possible_path = os.path.join("modules", f"{module_name}{ext}")
        if os.path.exists(possible_path):
            native_module_path = possible_path
            break

    if native_module_path:
        if native_module_path in LOADED_MODULES:
            return None
            
        LOADED_MODULES.add(native_module_path)
        try:
            spec = importlib.util.spec_from_file_location(module_name, native_module_path)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            
            # Executed custom module initialization if a setup() hook exists
            if hasattr(module, "setup"):
                module.setup(evaluator)
            else:
                # Expose all public variables, methods and constants directly to the global scope
                for attr_name in dir(module):
                    if not attr_name.startswith('_'):
                        attr_value = getattr(module, attr_name)
                        evaluator.global_env.define(attr_name, attr_value)
            return None
        except Exception as e:
            raise NixRuntimeError(f"Failed to load local native module '{module_name}': {e}", node)

    # 2. Otherwise, fall back to loading a standard Nix script (.nx)
    module_file = f"{module_name}.nx"
    if module_file in LOADED_MODULES:
        return None

    if os.path.exists(module_file):
        LOADED_MODULES.add(module_file)
        try:
            with open(module_file, 'r', encoding='utf-8') as f:
                module_code = f.read()
                
            lexer = Lexer(module_code)
            tokens = lexer.tokenize()
            parser = Parser(tokens)
            module_ast = parser.parse()
            
            evaluator.evaluate(module_ast, evaluator.global_env)
            return None
        except Exception as e:
            if module_file in LOADED_MODULES:
                LOADED_MODULES.remove(module_file)
            raise NixRuntimeError(f"Failed inside Nix module '{module_name}': {e}", node)

    # 3. Last fallback: Try importing directly from Python's standard library (e.g., math, random)
    if module_name in LOADED_MODULES:
        return None

    try:
        module = importlib.import_module(module_name)
        LOADED_MODULES.add(module_name)
        
        if hasattr(module, "setup"):
            module.setup(evaluator)
        else:
            # Automatically bind all public module functions/constants to global variables
            for attr_name in dir(module):
                if not attr_name.startswith('_'):
                    attr_value = getattr(module, attr_name)
                    evaluator.global_env.define(attr_name, attr_value)
        return None
    except ImportError:
        # If all paths (local .py/.so/.pyd, local .nx, standard library) fail, raise error
        raise NixRuntimeError(
            f"Module Import Error: Cannot find module '{module_name}' locally or in Python system libraries", 
            node
        )


def setup_modules(evaluator):
    """
    Registers the 'using' syntax evaluation handler in the main evaluator.
    """
    evaluator.register_builtin(UsingNode, eval_using)


def clear_module_cache():
    """
    Utility function to clear the loaded modules cache.
    """
    LOADED_MODULES.clear()

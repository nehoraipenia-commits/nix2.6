import sys
from ast_nodes import OutNode, CinCallNode, VectorNode, DictionaryNode, SubscriptNode
from environment import NixRuntimeError

def format_nix_value(val):
    """
    Recursively formats Python native values back to their 
    corresponding Nix-style string representations.
    - True/False becomes true/false (lowercase).
    - Lists become vectors printed as [val1, val2, ...].
    - Dicts become dictionaries printed as {key1: val1, key2: val2}.
    """
    if val is True:
        return "true"
    elif val is False:
        return "false"
    elif isinstance(val, list):
        formatted_elements = [format_nix_value(elem) for elem in val]
        return "[" + ", ".join(formatted_elements) + "]"
    elif isinstance(val, dict):
        formatted_pairs = []
        for k, v in val.items():
            formatted_pairs.append(f"{format_nix_value(k)}: {format_nix_value(v)}")
        return "{" + ", ".join(formatted_pairs) + "}"
    return str(val)


# ==============================================================================
# BUILTIN EVALUATOR HANDLERS
# ==============================================================================

def eval_out(node, env, evaluator):
    """
    Handles evaluation of the 'out(...)' output function.
    Evaluates all argument expressions and prints them to the terminal.
    """
    evaluated_args = []
    for arg in node.arguments:
        val = evaluator.evaluate(arg, env)
        evaluated_args.append(format_nix_value(val))
    
    # Print elements separated by spaces
    print(" ".join(evaluated_args))
    return None


def eval_cin(node, env, evaluator):
    prompt_text = ""
    # Si le nœud contient une expression de texte, on l'évalue
    if node.prompt_expr is not None:
        prompt_text = format_nix_value(evaluator.evaluate(node.prompt_expr, env))
        
    # On passe le texte directement à la fonction input() de Python
    user_input = input(prompt_text)
    
    # Gestion des types automatiques (convertit en nombre si c'est possible)
    if user_input.isdigit():
        return int(user_input)
    try:
        return float(user_input)
    except ValueError:
        if user_input.lower() == "true": return True
        if user_input.lower() == "false": return False
        return user_input


def eval_vector(node, env, evaluator):
    """
    Handles evaluation of sequential data vectors (lists).
    Evaluates all individual inner element expressions and returns a Python list.
    """
    evaluated_elements = []
    for element in node.elements:
        val = evaluator.evaluate(element, env)
        evaluated_elements.append(val)
    return evaluated_elements


def eval_dictionary(node, env, evaluator):
    """
    Handles evaluation of associative dictionary nodes.
    Evaluates all key and value expressions, producing a Python dictionary.
    """
    evaluated_dict = {}
    for key_node, val_node in node.key_value_pairs.items():
        evaluated_key = evaluator.evaluate(key_node, env)
        
        # Keys must be hashable types
        if isinstance(evaluated_key, (list, dict)):
            raise NixRuntimeError("Dictionary key must be a primitive hashable type (string, number, boolean)", key_node)
            
        evaluated_val = evaluator.evaluate(val_node, env)
        evaluated_dict[evaluated_key] = evaluated_val
    return evaluated_dict


def eval_subscript(node, env, evaluator):
    """
    Handles retrieval of elements from indexing subscripts: collection[index_expr].
    Supports list index lookups and dictionary key accesses.
    """
    collection = evaluator.evaluate(node.collection, env)
    index = evaluator.evaluate(node.index_expr, env)

    if isinstance(collection, list):
        if not isinstance(index, int):
            raise NixRuntimeError(
                f"Vector subscription index must be an integer, got '{type(index).__name__}'", 
                node.index_expr
            )
        try:
            return collection[index]
        except IndexError:
            raise NixRuntimeError(
                f"Vector index out of bounds: {index} (vector size is {len(collection)})", 
                node
            )

    elif isinstance(collection, dict):
        try:
            return collection[index]
        except KeyError:
            raise NixRuntimeError(
                f"Key Error: '{format_nix_value(index)}' not found in dictionary", 
                node
            )

    raise NixRuntimeError(
        f"Subscript lookup target must be a Vector or a Dictionary, got '{type(collection).__name__}'", 
        node.collection
    )


# ==============================================================================
# REGISTRATION INTERFACE
# ==============================================================================

def setup_builtins(evaluator):
    """
    Registers all built-in Nix nodes within the evaluator's handler registry.
    """
    evaluator.register_builtin(OutNode, eval_out)
    evaluator.register_builtin(CinCallNode, eval_cin)
    evaluator.register_builtin(VectorNode, eval_vector)
    evaluator.register_builtin(DictionaryNode, eval_dictionary)
    evaluator.register_builtin(SubscriptNode, eval_subscript)

import math
from decimal import Decimal, getcontext
from ast_nodes import FuncCallNode
from environment import NixRuntimeError

# Set decimal precision to 110 to comfortably hold 100 decimal places of Pi
getcontext().prec = 110

# Pi constant defined with exactly 100 decimal places as a high-precision string
PI_100_STR = "3.1415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679"

# ==============================================================================
# HIGH-PRECISION MATHEMATICAL FUNCTIONS (DECIMAL-BASED)
# ==============================================================================

def nix_math_get_pi(args, node):
    """
    Returns Pi with 100 decimal places as a high-precision Decimal object.
    Usage in Nix: my_pi = math_get_pi()
    """
    return Decimal(PI_100_STR)


def nix_math_high_precision_mul(args, node):
    """
    Multiplies two numbers with up to 100 decimal places of precision.
    Usage in Nix: result = hp_mul(val1, val2)
    """
    if len(args) < 2:
        raise NixRuntimeError("hp_mul expects 2 arguments: (val1, val2)", node)
    try:
        return Decimal(str(args[0])) * Decimal(str(args[1]))
    except Exception as e:
        raise NixRuntimeError(f"High-precision multiplication failed: {e}", node)


def nix_math_high_precision_div(args, node):
    """
    Divides two numbers with up to 100 decimal places of precision.
    Usage in Nix: result = hp_div(val1, val2)
    """
    if len(args) < 2:
        raise NixRuntimeError("hp_div expects 2 arguments: (val1, val2)", node)
    try:
        denom = Decimal(str(args[1]))
        if denom == 0:
            raise NixRuntimeError("High-precision division by zero", node)
        return Decimal(str(args[0])) / denom
    except Exception as e:
        raise NixRuntimeError(f"High-precision division failed: {e}", node)

# ==============================================================================
# STANDARD MATHEMATICAL FUNCTIONS (FLOAT-BASED)
# ==============================================================================

def nix_math_abs(args, node):
    """Returns the absolute value of a number."""
    if len(args) < 1:
        raise NixRuntimeError("math_abs expects 1 argument: (x)", node)
    return abs(args[0])


def nix_math_sqrt(args, node):
    """Returns the square root of a number."""
    if len(args) < 1:
        raise NixRuntimeError("math_sqrt expects 1 argument: (x)", node)
    if args[0] < 0:
        raise NixRuntimeError("Cannot calculate the square root of a negative number", node)
    return math.sqrt(args[0])


def nix_math_ceil(args, node):
    """Rounds a number up to the nearest integer."""
    if len(args) < 1:
        raise NixRuntimeError("math_ceil expects 1 argument: (x)", node)
    return math.ceil(args[0])


def nix_math_floor(args, node):
    """Rounds a number down to the nearest integer."""
    if len(args) < 1:
        raise NixRuntimeError("math_floor expects 1 argument: (x)", node)
    return math.floor(args[0])


def nix_math_pow(args, node):
    """Returns base raised to the power of exponent."""
    if len(args) < 2:
        raise NixRuntimeError("math_pow expects 2 arguments: (base, exponent)", node)
    return math.pow(args[0], args[1])


def nix_math_sin(args, node):
    """Returns the sine of x (measured in radians)."""
    if len(args) < 1:
        raise NixRuntimeError("math_sin expects 1 argument: (x_radians)", node)
    return math.sin(args[0])


def nix_math_cos(args, node):
    """Returns the cosine of x (measured in radians)."""
    if len(args) < 1:
        raise NixRuntimeError("math_cos expects 1 argument: (x_radians)", node)
    return math.cos(args[0])


def nix_math_tan(args, node):
    """Returns the tangent of x (measured in radians)."""
    if len(args) < 1:
        raise NixRuntimeError("math_tan expects 1 argument: (x_radians)", node)
    return math.tan(args[0])


def nix_math_log(args, node):
    """Returns the natural logarithm of x (or log with custom base if provided)."""
    if len(args) < 1:
        raise NixRuntimeError("math_log expects at least 1 argument: (x, [base])", node)
    if args[0] <= 0:
        raise NixRuntimeError("Logarithm argument must be greater than zero", node)
    if len(args) >= 2:
        return math.log(args[0], args[1])
    return math.log(args[0])


def nix_math_factorial(args, node):
    """Returns the factorial of a non-negative integer."""
    if len(args) < 1:
        raise NixRuntimeError("math_factorial expects 1 argument: (n)", node)
    val = args[0]
    if not isinstance(val, int) or val < 0:
        raise NixRuntimeError("Factorial requires a non-negative integer", node)
    return math.factorial(val)

# ==============================================================================
# DISPATCHER AND ROUTER INTERFACE
# ==============================================================================

MATH_FUNCTIONS = {
    # High-precision decimals
    "math_get_pi": nix_math_get_pi,
    "hp_mul": nix_math_high_precision_mul,
    "hp_div": nix_math_high_precision_div,
    
    # Standard math
    "math_abs": nix_math_abs,
    "math_sqrt": nix_math_sqrt,
    "math_ceil": nix_math_ceil,
    "math_floor": nix_math_floor,
    "math_pow": nix_math_pow,
    "math_sin": nix_math_sin,
    "math_cos": nix_math_cos,
    "math_tan": nix_math_tan,
    "math_log": nix_math_log,
    "math_factorial": nix_math_factorial,
}

def setup(evaluator):
    """
    Standard dynamic registration interface called when executing 'using <math>'.
    Preserves and chains previous custom function handlers (like sockets) if they exist.
    """
    # 1. Inject math global constants directly into the runtime global environment
    evaluator.global_env.define("pi_str", PI_100_STR)
    evaluator.global_env.define("pi", 3.141592653589793)
    evaluator.global_env.define("e", 2.718281828459045)

    # 2. Intercept FuncCallNode evaluations with a smart fallback chain
    previous_handler = evaluator.builtin_handlers.get(FuncCallNode)

    def eval_math_func_call(node, env, evaluator):
        if node.name in MATH_FUNCTIONS:
            evaluated_args = [evaluator.evaluate(arg, env) for arg in node.arguments]
            return MATH_FUNCTIONS[node.name](evaluated_args, node)
        
        # Fallback to the previously registered custom handler (e.g., socket_module)
        if previous_handler:
            return previous_handler(node, env, evaluator)
            
        # Standard fallback to the default interpreter's user-defined functions
        return evaluator.eval_func_call(node, env)

    evaluator.register_builtin(FuncCallNode, eval_math_func_call)

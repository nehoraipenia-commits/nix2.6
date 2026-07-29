import sys
import os
import nix_compiler
# Import all modular components of the Nix Interpreter
from lexer import Lexer
from nix_parser import Parser
from environment import Environment, NixRuntimeError
from evaluator import Evaluator
from nix_builtins import setup_builtins
from modules import setup_modules, clear_module_cache

RED = "\033[91m"
YELLOW = "\033[93m"
BLUE = "\033[94m"
RESET = "\033[0m"
GRAS = "\033[1m"

def main():
    """
    Main execution CLI entry point for the Nix programming language interpreter.
    Usage: python3 nix.py <filename.nx>
    """
    if len(sys.argv) < 3:
        print("Usage: nix <command> <filename.nx>")
        sys.exit(1)

    cmd = sys.argv[1]
    filepath = sys.argv[2]

    if cmd == "run":
        pass  

    elif cmd == "build":
        input_file = sys.argv[2]

        if "-o" in sys.argv:
            output = sys.argv[sys.argv.index("-o") + 1]
        else:
            output = "out"

        nix_compiler.compile_file(input_file, output)
        return


    #filepath = sys.argv[1]
    if not os.path.exists(filepath):
        print(f"Error: Source file '{filepath}' not found.")
        sys.exit(1)

    # Read the main script file
    try:
        with open(filepath, 'r', encoding='utf-8') as file:
            source_code = file.read()
    except Exception as e:
        print(f"Error: Failed to read file '{filepath}': {e}", file=sys.stderr)
        sys.exit(1)

    # Initialize the execution environment and core evaluator engine
    global_env = Environment()
    evaluator = Evaluator(global_env)

    # Register interactive I/O functions and collection helpers (out, cin, vectors, dicts)
    setup_builtins(evaluator)

    # Register the modular import system (using <...>) linked to modules.py
    setup_modules(evaluator)
    
    # Reset module caches for a fresh execution run
    clear_module_cache()

    try:
        # Step 1: Run the Lexer to transform source code into a Token Stream
        lexer = Lexer(source_code)
        tokens = lexer.tokenize()

        # Step 2: Run the Parser to construct the Abstract Syntax Tree (AST)
        parser = Parser(tokens)
        ast = parser.parse()

        # Step 3: Run the Evaluator to interpret the AST nodes
        evaluator.evaluate(ast, global_env)

    except SyntaxError as se:
        print(f"\n{RED}{GRAS}[Nix Compilation Failed]{RESET}")
        print(f"{YELLOW}{se}{RESET}", file=sys.stderr)
        sys.exit(1)
        
    except NixRuntimeError as re:
        print(f"\n{RED}{GRAS}[Nix Execution Halted]{RESET}")
        print(f"{RED}{re}{RESET}", file=sys.stderr)
        sys.exit(1)
        
    except Exception as e:
        print(f"\n{RED}{GRAS}[Internal Interpreter Error]{RESET}")
        print(f"{RED}{e}{RESET}", file=sys.stderr)
        sys.exit(1)

    except KeyboardInterrupt:
        print(f"\n{RED}{GRAS}[Internal Interpreter Error]{RESET}")
        print(f"{RED}Keyboard Interrupt{RESET}", file=sys.stderr)
        sys.exit(1)

if __name__ == '__main__':
    main()

# -*- coding: utf-8 -*-
"""
Native System and Files Module for the Nix interpreter.
This module allows Nix scripts to interact with the OS,
read/write files, and retrieve system information.
"""

import os
import sys
import subprocess
from ast_nodes import FuncCallNode
from environment import NixRuntimeError

# ==============================================================================
# NATIVE SYSTEM FUNCTIONS REGISTER
# ==============================================================================

def nix_file_write(args, node):
    """
    Writes content to a file. Overwrites the file if it already exists.
    Nix usage: file_write("save.txt", "Score: 100")
    """
    if len(args) < 2:
        raise NixRuntimeError("file_write requires 2 arguments: (file_path, content)", node)
    
    filepath = str(args[0])
    content = str(args[1])
    
    try:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        return True
    except Exception as e:
        raise NixRuntimeError(f"Failed to write to file '{filepath}': {e}", node)


def nix_file_read(args, node):
    """
    Reads and returns the contents of a file as a string.
    Nix usage: content = file_read("config.cfg")
    """
    if len(args) < 1:
        raise NixRuntimeError("file_read requires 1 argument: (file_path)", node)
    
    filepath = str(args[0])
    
    if not os.path.exists(filepath):
        raise NixRuntimeError(f"File '{filepath}' does not exist.", node)
        
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            return f.read()
    except Exception as e:
        raise NixRuntimeError(f"Failed to read file '{filepath}': {e}", node)


def nix_file_exists(args, node):
    """
    Checks if a file or directory exists. Returns a boolean.
    Nix usage: if file_exists("save.txt") { ... }
    """
    if len(args) < 1:
        raise NixRuntimeError("file_exists requires 1 argument: (path)", node)
    return os.path.exists(str(args[0]))


def nix_file_delete(args, node):
    """
    Deletes a file from the disk.
    Nix usage: file_delete("old_file.txt")
    """
    if len(args) < 1:
        raise NixRuntimeError("file_delete requires 1 argument: (file_path)", node)
    
    filepath = str(args[0])
    try:
        if os.path.exists(filepath):
            os.remove(filepath)
            return True
        return False
    except Exception as e:
        raise NixRuntimeError(f"Failed to delete file '{filepath}': {e}", node)


def nix_sys_args(args, node):
    """
    Returns the list of command line arguments passed to the Nix script.
    Nix usage: my_args = sys_args()
    """
    # Returns arguments after the main script name
    return sys.argv[2:] if len(sys.argv) > 2 else []


def nix_sys_execute(args, node):
    """
    Executes a system command in the terminal and returns its result.
    Nix usage: file_list = sys_execute("ls -la")
    """
    if len(args) < 1:
        raise NixRuntimeError("sys_execute requires 1 argument: (command)", node)
    
    command = str(args[0])
    try:
        result = subprocess.run(command, shell=True, capture_output=True, text=True, encoding='utf-8')
        return {
            "stdout": result.stdout,
            "stderr": result.stderr,
            "code": result.returncode
        }
    except Exception as e:
        raise NixRuntimeError(f"Error executing command '{command}': {e}", node)


def nix_os_name(args, node):
    """
    Returns the current OS name ('posix', 'nt', etc.).
    Nix usage: os = os_name()
    """
    return os.name


def nix_os_listdir(args, node):
    """
    Returns a list of files and folders present in a directory.
    Nix usage: elements = os_listdir(".")
    """
    path = str(args[0]) if len(args) > 0 else "."
    try:
        return os.listdir(path)
    except Exception as e:
        raise NixRuntimeError(f"Failed to list directory '{path}': {e}", node)


# ==============================================================================
# ROUTER AND INTERPRETER SETUP
# ==============================================================================

SYS_FUNCTIONS = {
    "file_write": nix_file_write,
    "file_read": nix_file_read,
    "file_exists": nix_file_exists,
    "file_delete": nix_file_delete,
    "sys_args": nix_sys_args,
    "sys_execute": nix_sys_execute,
    "os_name": nix_os_name,
    "os_listdir": nix_os_listdir,
}

def setup(evaluator):
    """
    Registers native system features into the Nix environment.
    """
    previous_handler = evaluator.builtin_handlers.get(FuncCallNode)

    def eval_sys_func_call(node, env, evaluator):
        if node.name in SYS_FUNCTIONS:
            evaluated_args = [evaluator.evaluate(arg, env) for arg in node.arguments]
            return SYS_FUNCTIONS[node.name](evaluated_args, node)
        
        if previous_handler:
            return previous_handler(node, env, evaluator)
        return evaluator.eval_func_call(node, env)

    evaluator.register_builtin(FuncCallNode, eval_sys_func_call)

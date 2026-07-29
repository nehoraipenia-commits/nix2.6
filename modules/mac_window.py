import sys
import tkinter as tk
from ast_nodes import FuncCallNode
from environment import NixRuntimeError

IS_MAC = sys.platform.startswith("darwin")

WINDOW_REGISTRY = {}
_next_window_id = 1

def _get_window(window_id, node):
    if window_id not in WINDOW_REGISTRY:
        raise NixRuntimeError(f"Invalid or closed window handle: {window_id}", node)
    return WINDOW_REGISTRY[window_id]


class NativeWindowContext:
    def __init__(self, title, width, height):
        self.title = title
        self.width = width
        self.height = height
        self.is_running = True
        self.event_queue = []

        # macOS / Cross-platform Tkinter Window Engine
        self.root = tk.Tk()
        self.root.title(self.title)
        self.root.geometry(f"{self.width}x{self.height}")
        self.root.resizable(False, False)

        self.canvas = tk.Canvas(
            self.root, 
            width=self.width, 
            height=self.height, 
            bg="black", 
            highlightthickness=0
        )
        self.canvas.pack(fill="both", expand=True)

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.root.bind("<KeyPress>", self._on_key_press)

    def _on_close(self):
        self.is_running = False

    def _on_key_press(self, event):
        key = event.keysym.lower()
        if key in ("left", "right", "up", "down"):
            self.event_queue.append(key)
        elif event.char:
            self.event_queue.append(event.char)


# ==============================================================================
# API NIX WRAPPERS
# ==============================================================================

def nix_window_open(args, node):
    global _next_window_id
    if len(args) < 3:
        raise NixRuntimeError("window_open expects 3 arguments: (title, width, height)", node)
    
    title = str(args[0])
    width = int(args[1])
    height = int(args[2])

    try:
        win_ctx = NativeWindowContext(title, width, height)
        window_id = _next_window_id
        WINDOW_REGISTRY[window_id] = win_ctx
        _next_window_id += 1
        return window_id
    except Exception as e:
        raise NixRuntimeError(f"Failed to instantiate native GUI Window: {e}", node)


def nix_window_is_open(args, node):
    if len(args) < 1:
        raise NixRuntimeError("window_is_open expects 1 argument: (window_id)", node)
    win = _get_window(args[0], node)
    return win.is_running


def nix_window_poll_events(args, node):
    if len(args) < 1:
        raise NixRuntimeError("window_poll_events expects 1 argument: (window_id)", node)

    win = _get_window(args[0], node)
    if win.is_running:
        try:
            win.root.update_idletasks()
            win.root.update()
        except Exception:
            win.is_running = False

    if win.event_queue:
        return win.event_queue.pop(0)

    return ""


def nix_window_clear(args, node):
    if len(args) < 4:
        raise NixRuntimeError("window_clear expects 4 arguments: (window_id, r, g, b)", node)

    win = _get_window(args[0], node)
    r, g, b = int(args[1]) & 0xFF, int(args[2]) & 0xFF, int(args[3]) & 0xFF
    color_hex = f"#{r:02x}{g:02x}{b:02x}"
    
    win.canvas.delete("all")
    win.canvas.configure(bg=color_hex)
    return None


def nix_window_draw_rect(args, node):
    if len(args) < 8:
        raise NixRuntimeError("window_draw_rect expects 8 arguments", node)

    win = _get_window(args[0], node)
    x, y, w, h = int(args[1]), int(args[2]), int(args[3]), int(args[4])
    r, g, b = int(args[5]) & 0xFF, int(args[6]) & 0xFF, int(args[7]) & 0xFF
    filled = bool(args[8])
    color_hex = f"#{r:02x}{g:02x}{b:02x}"

    if filled:
        win.canvas.create_rectangle(x, y, x + w, y + h, fill=color_hex, outline="")
    else:
        win.canvas.create_rectangle(x, y, x + w, y + h, outline=color_hex)

    return None


def nix_window_draw_text(args, node):
    if len(args) < 7:
        raise NixRuntimeError("window_draw_text expects 7 arguments", node)

    win = _get_window(args[0], node)
    text_str = str(args[1])
    x, y = int(args[2]), int(args[3])
    r, g, b = int(args[4]) & 0xFF, int(args[5]) & 0xFF, int(args[6]) & 0xFF
    color_hex = f"#{r:02x}{g:02x}{b:02x}"

    win.canvas.create_text(x, y, text=text_str, fill=color_hex, anchor="nw", font=("Helvetica", 14, "bold"))
    return None


def nix_window_present(args, node):
    if len(args) < 1:
        raise NixRuntimeError("window_present expects 1 argument", node)

    win = _get_window(args[0], node)
    if win.is_running:
        try:
            win.root.update_idletasks()
            win.root.update()
        except Exception:
            win.is_running = False
    return None


def nix_window_close(args, node):
    if len(args) < 1:
        raise NixRuntimeError("window_close expects 1 argument", node)

    win_id = args[0]
    win = _get_window(win_id, node)
    win.is_running = False
    try:
        win.root.destroy()
    except Exception:
        pass

    if win_id in WINDOW_REGISTRY:
        del WINDOW_REGISTRY[win_id]
    return True


WINDOW_FUNCTIONS = {
    "window_open": nix_window_open,
    "window_is_open": nix_window_is_open,
    "window_poll_events": nix_window_poll_events,
    "window_clear": nix_window_clear,
    "window_draw_rect": nix_window_draw_rect,
    "window_draw_text": nix_window_draw_text,
    "window_present": nix_window_present,
    "window_close": nix_window_close,
}


def setup(evaluator):
    previous_handler = evaluator.builtin_handlers.get(FuncCallNode)

    def eval_window_func_call(node, env, evaluator):
        if node.name in WINDOW_FUNCTIONS:
            evaluated_args = [evaluator.evaluate(arg, env) for arg in node.arguments]
            return WINDOW_FUNCTIONS[node.name](evaluated_args, node)

        if previous_handler:
            return previous_handler(node, env, evaluator)

        return evaluator.eval_func_call(node, env)

    evaluator.register_builtin(FuncCallNode, eval_window_func_call)

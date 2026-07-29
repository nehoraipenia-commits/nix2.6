import os
import sys
import time
import ctypes
from ctypes import (
    c_int, c_uint, c_char_p, c_void_p, c_long, c_ulong, c_ushort,
    POINTER, Structure, Union, CFUNCTYPE, byref, sizeof, cast
)
from ast_nodes import FuncCallNode
from environment import NixRuntimeError

# ==============================================================================
# CROSS-PLATFORM SYSTEM DETECTOR & CONFIGURATION
# ==============================================================================

IS_WINDOWS = sys.platform.startswith("win")
IS_LINUX = sys.platform.startswith("linux")
IS_MAC = sys.platform.startswith("darwin")

# Setup safe handles registry
WINDOW_REGISTRY = {}
_next_window_id = 1

def _get_window(window_id, node):
    """Retrieves an active native window context from the registry."""
    if window_id not in WINDOW_REGISTRY:
        raise NixRuntimeError(f"Invalid or closed window handle: {window_id}", node)
    return WINDOW_REGISTRY[window_id]


# ==============================================================================
# NATIVE WIN32 BINDINGS (WINDOWS)
# ==============================================================================

if IS_WINDOWS:
    user32 = ctypes.windll.user32
    gdi32 = ctypes.windll.gdi32
    kernel32 = ctypes.windll.kernel32

    # Win32 Data Types and Constants
    LRESULT = c_long
    HWND = c_void_p
    UINT = c_uint
    WPARAM = c_ulong
    LPARAM = c_long
    HDC = c_void_p
    HBRUSH = c_void_p
    HFONT = c_void_p
    HBITMAP = c_void_p
    HINSTANCE = c_void_p
    HICON = c_void_p
    HCURSOR = c_void_p
    ATOM = c_ushort

    CS_HREDRAW = 0x0002
    CS_VREDRAW = 0x0001
    WS_OVERLAPPEDWINDOW = 0x00CF0000
    WS_VISIBLE = 0x10000000
    CW_USEDEFAULT = 0x80000000
    SW_SHOW = 5

    WM_DESTROY = 0x0002
    WM_CLOSE = 0x0010
    WM_PAINT = 0x000F
    WM_KEYDOWN = 0x0100
    WM_KEYUP = 0x0101
    WM_LBUTTONDOWN = 0x0201
    WM_LBUTTONUP = 0x0202
    WM_RBUTTONDOWN = 0x0204
    WM_RBUTTONUP = 0x0205
    WM_MOUSEMOVE = 0x0200

    COLOR_WINDOW = 5
    IDI_APPLICATION = 32512
    IDC_ARROW = 32512

    # Structures
    class WNDCLASSEXW(Structure):
        _fields_ = [
            ("cbSize", UINT),
            ("style", UINT),
            ("lpfnWndProc", CFUNCTYPE(LRESULT, HWND, UINT, WPARAM, LPARAM)),
            ("cbClsExtra", c_int),
            ("cbWndExtra", c_int),
            ("hInstance", HINSTANCE),
            ("hIcon", HICON),
            ("hCursor", HCURSOR),
            ("hbrBackground", HBRUSH),
            ("lpszMenuName", c_char_p),
            ("lpszClassName", c_char_p),
            ("hIconSm", HICON)
        ]

    class POINT(Structure):
        _fields_ = [("x", c_long), ("y", c_long)]

    class MSG(Structure):
        _fields_ = [
            ("hwnd", HWND),
            ("message", UINT),
            ("wParam", WPARAM),
            ("lParam", LPARAM),
            ("time", UINT),
            ("pt", POINT)
        ]

    class RECT(Structure):
        _fields_ = [
            ("left", c_long),
            ("top", c_long),
            ("right", c_long),
            ("bottom", c_long)
        ]

    class PAINTSTRUCT(Structure):
        _fields_ = [
            ("hdc", HDC),
            ("fErase", c_int),
            ("rcPaint", RECT),
            ("fRestore", c_int),
            ("fIncUpdate", c_int),
            ("rgbReserved", c_int * 32)
        ]


# ==============================================================================
# NATIVE X11 BINDINGS (LINUX)
# ==============================================================================

if IS_LINUX:
    try:
        x11 = ctypes.cdll.LoadLibrary("libX11.so.6")
    except OSError:
        try:
            x11 = ctypes.cdll.LoadLibrary("libX11.so")
        except OSError:
            x11 = None

    if x11:
        # Types
        Display = c_void_p
        XWindow = c_ulong
        GC = c_void_p
        Visual = c_void_p
        XColormap = c_ulong

        # KeySym Mapping
        NoSymbol = 0

        # Structures
        class XInitThreads(Structure):
            pass

        class XEventUnion(Union):
            pass

        class XEvent(Structure):
            pass

        class XAnyEvent(Structure):
            _fields_ = [
                ("type", c_int),
                ("serial", c_ulong),
                ("send_event", c_int),
                ("display", Display),
                ("window", XWindow)
            ]

        class XKeyEvent(Structure):
            _fields_ = [
                ("type", c_int),
                ("serial", c_ulong),
                ("send_event", c_int),
                ("display", Display),
                ("window", XWindow),
                ("root", XWindow),
                ("subwindow", XWindow),
                ("time", c_ulong),
                ("x", c_int), ("y", c_int),
                ("x_root", c_int), ("y_root", c_int),
                ("state", c_uint),
                ("keycode", c_uint),
                ("same_screen", c_int)
            ]

        class XButtonEvent(Structure):
            _fields_ = [
                ("type", c_int),
                ("serial", c_ulong),
                ("send_event", c_int),
                ("display", Display),
                ("window", XWindow),
                ("root", XWindow),
                ("subwindow", XWindow),
                ("time", c_ulong),
                ("x", c_int), ("y", c_int),
                ("x_root", c_int), ("y_root", c_int),
                ("state", c_uint),
                ("button", c_uint),
                ("same_screen", c_int)
            ]

        class XMotionEvent(Structure):
            _fields_ = [
                ("type", c_int),
                ("serial", c_ulong),
                ("send_event", c_int),
                ("display", Display),
                ("window", XWindow),
                ("root", XWindow),
                ("subwindow", XWindow),
                ("time", c_ulong),
                ("x", c_int), ("y", c_int),
                ("x_root", c_int), ("y_root", c_int),
                ("state", c_uint),
                ("is_hint", c_int),
                ("same_screen", c_int)
            ]

        class XClientMessageEvent(Structure):
            _fields_ = [
                ("type", c_int),
                ("serial", c_ulong),
                ("send_event", c_int),
                ("display", Display),
                ("window", XWindow),
                ("message_type", c_ulong),
                ("format", c_int),
                ("data", c_long * 5)
            ]

        XEventUnion._fields_ = [
            ("type", c_int),
            ("xany", XAnyEvent),
            ("xkey", XKeyEvent),
            ("xbutton", XButtonEvent),
            ("xmotion", XMotionEvent),
            ("xclient", XClientMessageEvent),
            ("pad", c_long * 24)
        ]

        XEvent._fields_ = [("u", XEventUnion)]

        # Event Definitions
        KeyPress = 2
        KeyRelease = 3
        ButtonPress = 4
        ButtonRelease = 5
        MotionNotify = 6
        Expose = 12
        ClientMessage = 33

        KeyPressMask = (1 << 0)
        KeyReleaseMask = (1 << 1)
        ButtonPressMask = (1 << 2)
        ButtonReleaseMask = (1 << 3)
        PointerMotionMask = (1 << 6)
        ExposureMask = (1 << 15)
        StructureNotifyMask = (1 << 17)


# ==============================================================================
# NATIVE WINDOW CLASS REPRESENTATION
# ==============================================================================

class NativeWindowContext:
    """
    Abstration class wrapping platform-specific window handles,
    drawing canvases, double buffering memory bitmaps, and local input event queues.
    """
    def __init__(self, title, width, height):
        self.title = title
        self.width = width
        self.height = height
        self.is_running = True
        
        # Event States
        self.mouse_x = 0
        self.mouse_y = 0
        self.mouse_left = False
        self.mouse_right = False
        self.last_key = ""
        self.last_key_state = ""
        self.event_queue = []

        # Platform specifics initialization
        if IS_WINDOWS:
            self._init_win32()
        elif IS_LINUX and x11:
            self._init_x11()
        else:
            self._init_fallback()

    # --------------------------------------------------------------------------
    # WINDOWS WIN32 IMPLEMENTATION
    # --------------------------------------------------------------------------
    def _init_win32(self):
        # Create standard window procedure callback
        self.wndproc_ptr = CFUNCTYPE(LRESULT, HWND, UINT, WPARAM, LPARAM)(self._win32_wndproc)

        # Register Window Class
        self.class_name = f"NixWindowClass_{id(self)}"
        wclass = WNDCLASSEXW()
        wclass.cbSize = sizeof(WNDCLASSEXW)
        wclass.style = CS_HREDRAW | CS_VREDRAW
        wclass.lpfnWndProc = self.wndproc_ptr
        wclass.cbClsExtra = 0
        wclass.cbWndExtra = 0
        wclass.hInstance = kernel32.GetModuleHandleW(None)
        wclass.hIcon = user32.LoadIconW(None, IDI_APPLICATION)
        wclass.hCursor = user32.LoadCursorW(None, IDC_ARROW)
        wclass.hbrBackground = COLOR_WINDOW
        wclass.lpszMenuName = None
        wclass.lpszClassName = self.class_name.encode('utf-16le') + b'\x00\x00'
        wclass.hIconSm = None

        if not user32.RegisterClassExW(byref(wclass)):
            raise RuntimeError("Failed to register Windows Window Class")

        # Create Window Handle
        self.hwnd = user32.CreateWindowExW(
            0,
            self.class_name.encode('utf-16le') + b'\x00\x00',
            self.title.encode('utf-16le') + b'\x00\x00',
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT,
            self.width + 16, self.height + 39, # Offset for standard borders
            None, None,
            kernel32.GetModuleHandleW(None),
            None
        )

        if not self.hwnd:
            raise RuntimeError("Failed to create Win32 window")

        # Setup Double Buffering Contexts
        self.hdc = user32.GetDC(self.hwnd)
        self.mem_dc = gdi32.CreateCompatibleDC(self.hdc)
        self.mem_bitmap = gdi32.CreateCompatibleBitmap(self.hdc, self.width, self.height)
        gdi32.SelectObject(self.mem_dc, self.mem_bitmap)

        # Draw clean initial background
        brush = gdi32.CreateSolidBrush(0x00000000) # Pure Black (RGB color bytes: 0x00BBGGRR)
        rect = RECT(0, 0, self.width, self.height)
        user32.FillRect(self.mem_dc, byref(rect), brush)
        gdi32.DeleteObject(brush)

    def _win32_wndproc(self, hwnd, msg, wparam, lparam):
        """Win32 Callback handling and translating window inputs."""
        if msg == WM_DESTROY or msg == WM_CLOSE:
            self.is_running = False
            user32.PostQuitMessage(0)
            return 0
        
        elif msg == WM_MOUSEMOVE:
            self.mouse_x = lparam & 0xFFFF
            self.mouse_y = (lparam >> 16) & 0xFFFF
            self.event_queue.append({
                "type": "motion",
                "x": self.mouse_x,
                "y": self.mouse_y
            })
            return 0
            
        elif msg == WM_LBUTTONDOWN:
            self.mouse_left = True
            self.event_queue.append({"type": "click", "button": "left", "state": "down", "x": self.mouse_x, "y": self.mouse_y})
            return 0
            
        elif msg == WM_LBUTTONUP:
            self.mouse_left = False
            self.event_queue.append({"type": "click", "button": "left", "state": "up", "x": self.mouse_x, "y": self.mouse_y})
            return 0

        elif msg == WM_RBUTTONDOWN:
            self.mouse_right = True
            self.event_queue.append({"type": "click", "button": "right", "state": "down", "x": self.mouse_x, "y": self.mouse_y})
            return 0

        elif msg == WM_RBUTTONUP:
            self.mouse_right = False
            self.event_queue.append({"type": "click", "button": "right", "state": "up", "x": self.mouse_x, "y": self.mouse_y})
            return 0

        elif msg in (WM_KEYDOWN, WM_KEYUP):
            state = "down" if msg == WM_KEYDOWN else "up"
            key_code = wparam
            char = chr(key_code) if 32 <= key_code <= 126 else f"keycode_{key_code}"
            self.last_key = char
            self.last_key_state = state
            self.event_queue.append({"type": "key", "key": char, "state": state})
            return 0

        return user32.DefWindowProcW(hwnd, msg, wparam, lparam)

    # --------------------------------------------------------------------------
    # LINUX X11 IMPLEMENTATION
    # --------------------------------------------------------------------------
    def _init_x11(self):
        # Open Connection to Default Display
        self.display = x11.XOpenDisplay(None)
        if not self.display:
            raise RuntimeError("Cannot open connection to X11 Server.")

        self.screen = x11.XDefaultScreen(self.display)
        self.root_window = x11.XRootWindow(self.display, self.screen)

        # Create Native Window
        self.x_window = x11.XCreateSimpleWindow(
            self.display,
            self.root_window,
            10, 10,
            self.width, self.height,
            1,
            x11.XBlackPixel(self.display, self.screen),
            x11.XBlackPixel(self.display, self.screen)
        )

        # Set Window Title
        x11.XStoreName(self.display, self.x_window, self.title.encode('utf-8'))

        # Select Event Masks
        event_mask = (KeyPressMask | KeyReleaseMask | ButtonPressMask |
                      ButtonReleaseMask | PointerMotionMask | ExposureMask |
                      StructureNotifyMask)
        x11.XSelectInput(self.display, self.x_window, event_mask)

        # Register Close Protocol Client Messages safely
        self.wm_protocols = x11.XInternAtom(self.display, b"WM_PROTOCOLS", False)
        self.wm_delete_window = x11.XInternAtom(self.display, b"WM_DELETE_WINDOW", False)
        protocols = (c_ulong * 1)(self.wm_delete_window)
        x11.XSetWMProtocols(self.display, self.x_window, protocols, 1)

        # Map Window to screen
        x11.XMapWindow(self.display, self.x_window)

        # Graphics Context Init
        self.gc = x11.XCreateGC(self.display, self.x_window, 0, None)
        x11.XSetForeground(self.display, self.gc, x11.XWhitePixel(self.display, self.screen))

        # Clear display screen
        x11.XFlush(self.display)

    # --------------------------------------------------------------------------
    # FALLBACK ENGINE FOR MACOS & OTHERS
    # --------------------------------------------------------------------------
    def _init_fallback(self):
        # Fallback uses a software buffer mapping and simulates simple loop operations
        self.buffer = [" " * self.width] * self.height
        print(f"[Nix Canvas Initialized]: Soft Framebuffer mapped {self.width}x{self.height} (Fallback Engine active)")


# ==============================================================================
# ENGINE CONTROL AND EVENTS ACTIONS
# ==============================================================================

def win32_event_loop(win):
    """Processes Win32 event messages without halting the loop."""
    msg = MSG()
    while user32.PeekMessageW(byref(msg), None, 0, 0, 1): # PM_REMOVE = 1
        user32.TranslateMessage(byref(msg))
        user32.DispatchMessageW(byref(msg))


def x11_event_loop(win):
    """Polls and transforms pending X11 server events."""
    event = XEvent()
    while x11.XPending(win.display):
        x11.XNextEvent(win.display, byref(event))
        evt_type = event.u.type

        if evt_type == KeyPress:
            win.event_queue.append({"type": "key", "key": "pressed", "state": "down"})
        elif evt_type == KeyRelease:
            win.event_queue.append({"type": "key", "key": "released", "state": "up"})
        elif evt_type == ButtonPress:
            btn = event.u.xbutton.button
            win.event_queue.append({
                "type": "click",
                "button": "left" if btn == 1 else "right",
                "state": "down",
                "x": event.u.xbutton.x,
                "y": event.u.xbutton.y
            })
        elif evt_type == ButtonRelease:
            btn = event.u.xbutton.button
            win.event_queue.append({
                "type": "click",
                "button": "left" if btn == 1 else "right",
                "state": "up",
                "x": event.u.xbutton.x,
                "y": event.u.xbutton.y
            })
        elif evt_type == MotionNotify:
            win.mouse_x = event.u.xmotion.x
            win.mouse_y = event.u.xmotion.y
            win.event_queue.append({"type": "motion", "x": win.mouse_x, "y": win.mouse_y})
        elif evt_type == ClientMessage:
            # Match safely if delete client message received
            if event.u.xclient.data[0] == win.wm_delete_window:
                win.is_running = False


# ==============================================================================
# NATIVE WRAPPERS FOR NIX API BINDINGS
# ==============================================================================

def nix_window_open(args, node):
    """
    Creates and maps a native cross-platform Window instance.
    Usage in Nix: win = window_open("Nix Application Canvas", 800, 600)
    """
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
    """
    Returns true if the window context is still alive and running.
    Usage in Nix: status = window_is_open(win)
    """
    if len(args) < 1:
        raise NixRuntimeError("window_is_open expects 1 argument: (window_id)", node)
    
    win = _get_window(args[0], node)
    return win.is_running


def nix_window_poll_events(args, node):
    """
    Polls the operating system event loop queue and returns the latest event.
    Returns a Dictionary mapped with keys: type, key, button, state, x, y.
    Usage in Nix: event = window_poll_events(win)
    """
    if len(args) < 1:
        raise NixRuntimeError("window_poll_events expects 1 argument: (window_id)", node)

    win = _get_window(args[0], node)

    # Process native events to populate the local event queue
    if IS_WINDOWS:
        win32_event_loop(win)
    elif IS_LINUX and x11:
        x11_event_loop(win)

    # Pop and return the first event from queue
    if win.event_queue:
        raw_event = win.event_queue.pop(0)
        # Parse cleanly into a Nix-compatible dictionary format
        event_dict = {}
        for k, v in raw_event.items():
            event_dict[k] = v
        return event_dict

    # Return empty dictionary if no events are pending
    return {}


def nix_window_clear(args, node):
    """
    Clears the entire active drawing canvas using the specified color.
    Usage in Nix: window_clear(win, 0, 0, 0)
    """
    if len(args) < 4:
        raise NixRuntimeError("window_clear expects 4 arguments: (window_id, red, green, blue)", node)

    win = _get_window(args[0], node)
    r = int(args[1]) & 0xFF
    g = int(args[2]) & 0xFF
    b = int(args[3]) & 0xFF

    if IS_WINDOWS:
        # Resolve GDI Color formulation (Win32 format: 0x00BBGGRR)
        gdi_color = (b << 16) | (g << 8) | r
        brush = gdi32.CreateSolidBrush(gdi_color)
        rect = RECT(0, 0, win.width, win.height)
        user32.FillRect(win.mem_dc, byref(rect), brush)
        gdi32.DeleteObject(brush)
        
    elif IS_LINUX and x11:
        # Setup pure 24-bit X11 Color
        x_color = (r << 16) | (g << 8) | b
        x11.XSetForeground(win.display, win.gc, x_color)
        x11.XFillRectangle(win.display, win.x_window, win.gc, 0, 0, win.width, win.height)
        
    return None


def nix_window_draw_rect(args, node):
    """
    Draws or fills a rectangle shape on the window frame.
    Usage in Nix: window_draw_rect(win, 50, 50, 200, 150, 255, 0, 0, true)
    """
    if len(args) < 8:
        raise NixRuntimeError(
            "window_draw_rect expects 8 arguments: (window_id, x, y, width, height, red, green, blue, is_filled)", 
            node
        )

    win = _get_window(args[0], node)
    x = int(args[1])
    y = int(args[2])
    w = int(args[3])
    h = int(args[4])
    r = int(args[5]) & 0xFF
    g = int(args[6]) & 0xFF
    b = int(args[7]) & 0xFF
    filled = bool(args[8])

    if IS_WINDOWS:
        gdi_color = (b << 16) | (g << 8) | r
        if filled:
            brush = gdi32.CreateSolidBrush(gdi_color)
            rect = RECT(x, y, x + w, y + h)
            user32.FillRect(win.mem_dc, byref(rect), brush)
            gdi32.DeleteObject(brush)
        else:
            # Set Win32 pen brush for border drawing
            pen = gdi32.CreatePen(0, 1, gdi_color)
            old_pen = gdi32.SelectObject(win.mem_dc, pen)
            # Create transparent brush so fill isn't modified
            null_brush = gdi32.GetStockObject(5) # NULL_BRUSH = 5
            old_brush = gdi32.SelectObject(win.mem_dc, null_brush)
            
            gdi32.Rectangle(win.mem_dc, x, y, x + w, y + h)
            
            # Reset handles
            gdi32.SelectObject(win.mem_dc, old_pen)
            gdi32.SelectObject(win.mem_dc, old_brush)
            gdi32.DeleteObject(pen)

    elif IS_LINUX and x11:
        x_color = (r << 16) | (g << 8) | b
        x11.XSetForeground(win.display, win.gc, x_color)
        if filled:
            x11.XFillRectangle(win.display, win.x_window, win.gc, x, y, w, h)
        else:
            x11.XDrawRectangle(win.display, win.x_window, win.gc, x, y, w, h)

    return None


def nix_window_draw_line(args, node):
    """
    Draws a line between two coordinate points on the canvas.
    Usage in Nix: window_draw_line(win, 0, 0, 300, 300, 0, 255, 0)
    """
    if len(args) < 8:
        raise NixRuntimeError(
            "window_draw_line expects 8 arguments: (window_id, x1, y1, x2, y2, red, green, blue)", 
            node
        )

    win = _get_window(args[0], node)
    x1 = int(args[1])
    y1 = int(args[2])
    x2 = int(args[3])
    y2 = int(args[4])
    r = int(args[5]) & 0xFF
    g = int(args[6]) & 0xFF
    b = int(args[7]) & 0xFF

    if IS_WINDOWS:
        gdi_color = (b << 16) | (g << 8) | r
        pen = gdi32.CreatePen(0, 1, gdi_color)
        old_pen = gdi32.SelectObject(win.mem_dc, pen)
        
        gdi32.MoveToEx(win.mem_dc, x1, y1, None)
        gdi32.LineTo(win.mem_dc, x2, y2)
        
        gdi32.SelectObject(win.mem_dc, old_pen)
        gdi32.DeleteObject(pen)

    elif IS_LINUX and x11:
        x_color = (r << 16) | (g << 8) | b
        x11.XSetForeground(win.display, win.gc, x_color)
        x11.XDrawLine(win.display, win.x_window, win.gc, x1, y1, x2, y2)

    return None


def nix_window_draw_text(args, node):
    """
    Renders simple string text on the active frame coordinate system.
    Usage in Nix: window_draw_text(win, "Nix Graphics Engine", 100, 100, 255, 255, 255)
    """
    if len(args) < 7:
        raise NixRuntimeError(
            "window_draw_text expects 7 arguments: (window_id, text, x, y, red, green, blue)", 
            node
        )

    win = _get_window(args[0], node)
    text_str = str(args[1])
    x = int(args[2])
    y = int(args[3])
    r = int(args[4]) & 0xFF
    g = int(args[5]) & 0xFF
    b = int(args[6]) & 0xFF

    if IS_WINDOWS:
        gdi_color = (b << 16) | (g << 8) | r
        gdi32.SetTextColor(win.mem_dc, gdi_color)
        gdi32.SetBkMode(win.mem_dc, 1) # TRANSPARENT = 1
        
        # Win32 TextOutW handles wide UTF-16 character buffers
        text_utf16 = text_str.encode('utf-16le')
        gdi32.TextOutW(win.mem_dc, x, y, text_utf16, len(text_str))

    elif IS_LINUX and x11:
        x_color = (r << 16) | (g << 8) | b
        x11.XSetForeground(win.display, win.gc, x_color)
        x11.XDrawString(
            win.display, 
            win.x_window, 
            win.gc, 
            x, y, 
            text_str.encode('utf-8'), 
            len(text_str)
        )

    return None


def nix_window_present(args, node):
    """
    Flushes the memory drawing buffer onto the screen.
    Guarantees fluid refresh and double-buffering.
    Usage in Nix: window_present(win)
    """
    if len(args) < 1:
        raise NixRuntimeError("window_present expects 1 argument: (window_id)", node)

    win = _get_window(args[0], node)

    if IS_WINDOWS:
        # Copy the Memory DC buffer to the screen DC in one atomic operation
        gdi32.BitBlt(
            win.hdc, 
            0, 0, 
            win.width, win.height, 
            win.mem_dc, 
            0, 0, 
            0x00CC0020 # SRCCOPY = 0x00CC0020
        )
        
    elif IS_LINUX and x11:
        x11.XFlush(win.display)

    return None


def nix_window_close(args, node):
    """
    Closes the Window, releases native system buffers and deletes handle references.
    Usage in Nix: window_close(win)
    """
    if len(args) < 1:
        raise NixRuntimeError("window_close expects 1 argument: (window_id)", node)

    win_id = args[0]
    win = _get_window(win_id, node)

    if IS_WINDOWS:
        # GDI Cleanup
        gdi32.DeleteObject(win.mem_bitmap)
        gdi32.DeleteDC(win.mem_dc)
        user32.ReleaseDC(win.hwnd, win.hdc)
        user32.DestroyWindow(win.hwnd)
        
    elif IS_LINUX and x11:
        x11.XFreeGC(win.display, win.gc)
        x11.XDestroyWindow(win.display, win.x_window)
        x11.XCloseDisplay(win.display)

    # Erase reference registry handle
    del WINDOW_REGISTRY[win_id]
    return True

# ==============================================================================
# DISPATCHER AND ROUTER INTERFACE
# ==============================================================================

WINDOW_FUNCTIONS = {
    "window_open": nix_window_open,
    "window_is_open": nix_window_is_open,
    "window_poll_events": nix_window_poll_events,
    "window_clear": nix_window_clear,
    "window_draw_rect": nix_window_draw_rect,
    "window_draw_line": nix_window_draw_line,
    "window_draw_text": nix_window_draw_text,
    "window_present": nix_window_present,
    "window_close": nix_window_close,
}

def setup(evaluator):
    """
    Integrates the Window API functions seamlessly into the Nix execution environment.
    Supports smart call routing preserving other loaded native modules like Math & Sockets.
    """
    previous_handler = evaluator.builtin_handlers.get(FuncCallNode)

    def eval_window_func_call(node, env, evaluator):
        if node.name in WINDOW_FUNCTIONS:
            # Safely evaluate and convert AST arguments before execution
            evaluated_args = [evaluator.evaluate(arg, env) for arg in node.arguments]
            return WINDOW_FUNCTIONS[node.name](evaluated_args, node)
        
        # Fallback cleanly to previously chained native libraries
        if previous_handler:
            return previous_handler(node, env, evaluator)
            
        # Default user function fallback
        return evaluator.eval_func_call(node, env)

    evaluator.register_builtin(FuncCallNode, eval_window_func_call)

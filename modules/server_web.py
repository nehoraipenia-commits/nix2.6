import os
import re
import json
import urllib.parse
from wsgiref.simple_server import make_server
from typing import Callable, Dict, List, Tuple, Any, Optional, Union

# =============================================================================
# NIX ENVIRONMENT & AST INTERFACE (LIGHTWEIGHT EMBEDDED ENGINE)
# =============================================================================

class NixEnvironment:
    """
    Simulates or wraps a lexical scope environment for the Nix programming language.
    Stores variable bindings, functions, and registered web route handlers.
    """
    def __init__(self, parent: Optional['NixEnvironment'] = None):
        self.bindings: Dict[str, Any] = {}
        self.parent = parent

    def define(self, name: str, value: Any) -> None:
        """Defines or updates a variable symbol in the current scope."""
        self.bindings[name] = value

    def lookup(self, name: str) -> Any:
        """Looks up a variable symbol across scope hierarchy."""
        if name in self.bindings:
            return self.bindings[name]
        if self.parent:
            return self.parent.lookup(name)
        raise KeyError(f"Nix Environment Error: Symbol '{name}' is not defined.")

    def is_defined(self, name: str) -> bool:
        """Checks if a symbol exists in the current scope or parent scopes."""
        if name in self.bindings:
            return True
        if self.parent:
            return self.parent.is_defined(name)
        return False


# =============================================================================
# HTTP REQUEST & RESPONSE ENCAPSULATION
# =============================================================================

class NixWebRequest:
    """
    Represents an incoming HTTP Request received by the WSGI server.
    Parses headers, query parameters, form data, and JSON bodies into Python/Nix types.
    """
    def __init__(self, environ: Dict[str, Any]):
        self.environ = environ
        self.method: str = environ.get('REQUEST_METHOD', 'GET').upper()
        self.path: str = environ.get('PATH_INFO', '/')
        self.query_string: str = environ.get('QUERY_STRING', '')
        self.query_params: Dict[str, List[str]] = urllib.parse.parse_qs(self.query_string)
        self.headers: Dict[str, str] = self._parse_headers()
        self.body: bytes = b""
        self.form_data: Dict[str, List[str]] = {}
        self.json_data: Any = None
        self.path_params: Dict[str, Any] = {}
        
        self._parse_request_body()

    def _parse_headers(self) -> Dict[str, str]:
        """Extracts and normalizes HTTP request headers from the WSGI environment."""
        headers = {}
        for key, value in self.environ.items():
            if key.startswith('HTTP_'):
                header_name = key[5:].replace('_', '-').title()
                headers[header_name] = value
            elif key in ('CONTENT_TYPE', 'CONTENT_LENGTH'):
                header_name = key.replace('_', '-').title()
                headers[header_name] = value
        return headers

    def _parse_request_body(self) -> None:
        """Reads raw request bytes and parses HTML form submissions or JSON payloads."""
        try:
            content_length = int(self.environ.get('CONTENT_LENGTH', 0))
        except (ValueError, TypeError):
            content_length = 0

        if content_length <= 0:
            return

        wsgi_input = self.environ.get('wsgi.input')
        if wsgi_input:
            self.body = wsgi_input.read(content_length)

        content_type = self.headers.get('Content-Type', '')

        # Parse application/json payload
        if 'application/json' in content_type:
            try:
                self.json_data = json.loads(self.body.decode('utf-8'))
            except Exception:
                self.json_data = None

        # Parse application/x-www-form-urlencoded payload
        elif 'application/x-www-form-urlencoded' in content_type:
            try:
                decoded_body = self.body.decode('utf-8')
                self.form_data = urllib.parse.parse_qs(decoded_body)
            except Exception:
                self.form_data = {}

    def get_param(self, key: str, default: Optional[str] = None) -> Optional[str]:
        """Retrieves a single query parameter value by key."""
        values = self.query_params.get(key)
        return values[0] if values else default

    def get_form_field(self, key: str, default: Optional[str] = None) -> Optional[str]:
        """Retrieves a single submitted form field value by key."""
        values = self.form_data.get(key)
        return values[0] if values else default

    def to_nix_dict(self) -> Dict[str, Any]:
        """Converts the HTTP request into a structured dictionary accessible inside Nix scripts."""
        flat_query = {k: v[0] if len(v) == 1 else v for k, v in self.query_params.items()}
        flat_form = {k: v[0] if len(v) == 1 else v for k, v in self.form_data.items()}
        return {
            "method": self.method,
            "path": self.path,
            "query": flat_query,
            "form": flat_form,
            "json": self.json_data,
            "params": self.path_params,
            "headers": self.headers
        }


class NixWebResponse:
    """
    Represents an outgoing HTTP Response.
    Contains status code, HTTP headers, and raw content body bytes.
    """
    def __init__(self, body: Union[str, bytes] = "", status_code: int = 200, content_type: str = "text/html"):
        self.status_code = status_code
        self.headers: List[Tuple[str, str]] = [('Content-Type', f'{content_type}; charset=utf-8')]
        if isinstance(body, str):
            self.body = body.encode('utf-8')
        else:
            self.body = body

    def add_header(self, name: str, value: str) -> None:
        """Appends a custom HTTP response header."""
        self.headers.append((name, value))

    @classmethod
    def html(cls, html_code: str, status_code: int = 200) -> 'NixWebResponse':
        """Helper constructor for HTML responses."""
        return cls(body=html_code, status_code=status_code, content_type="text/html")

    @classmethod
    def json(cls, data: Any, status_code: int = 200) -> 'NixWebResponse':
        """Helper constructor for JSON API responses."""
        json_str = json.dumps(data, indent=2, ensure_ascii=False)
        return cls(body=json_str, status_code=status_code, content_type="application/json")

    @classmethod
    def redirect(cls, location: str, status_code: int = 302) -> 'NixWebResponse':
        """Helper constructor for HTTP URL redirects."""
        res = cls(body="", status_code=status_code)
        res.add_header('Location', location)
        return res


# =============================================================================
# TEMPLATE ENGINE FOR NIX (HTML INTERPOLATION)
# =============================================================================

class NixTemplateEngine:
    """
    Lightweight HTML Template Engine for Nix.
    Interpolates context variables like {{ var_name }} or {{ user.name }} inside HTML code.
    """
    @staticmethod
    def render_string(template_str: str, context: Dict[str, Any]) -> str:
        """Renders dynamic context dictionary into template placeholders {{ expression }}."""
        def replace_var(match):
            expr = match.group(1).strip()
            keys = expr.split('.')
            val = context
            for k in keys:
                if isinstance(val, dict):
                    val = val.get(k, '')
                elif hasattr(val, k):
                    val = getattr(val, k)
                else:
                    return ""
            return str(val)

        pattern = re.compile(r'\{\{\s*([a-zA-Z0-9_\.]+)\s*\}\}')
        return pattern.sub(replace_var, template_str)

    @classmethod
    def render_file(cls, filepath: str, context: Dict[str, Any]) -> str:
        """Loads an external HTML file from disk and renders template placeholders."""
        if not os.path.exists(filepath):
            return f"<h1>Template Error</h1><p>Template file '{filepath}' was not found.</p>"
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
        return cls.render_string(content, context)


# =============================================================================
# ROUTE MATCHING ENGINE (FLASK-STYLE PATH PATTERNS)
# =============================================================================

class NixWebRoute:
    """
    Matches incoming HTTP path and method against registered route patterns.
    Converts Flask-like specs (e.g. /user/<string:name> or /post/<int:id>) into Named Regex Groups.
    """
    TYPE_PATTERNS = {
        'string': r'[^/]+',
        'int': r'\d+',
        'float': r'\d+(?:\.\d+)?',
        'path': r'.+'
    }

    def __init__(self, path_pattern: str, methods: List[str], handler: Any):
        self.raw_path = path_pattern
        self.methods = [m.upper() for m in methods]
        self.handler = handler  # Python callable OR Nix function name
        self.param_types: Dict[str, str] = {}
        self.regex = self._build_regex(path_pattern)

    def _build_regex(self, path: str) -> re.Pattern:
        """Transforms route syntax into dynamic regular expression patterns."""
        pattern = r'<(?::?(?P<type>string|int|float|path):)?(?P<name>[a-zA-Z_][a-zA-Z0-9_]*)>'

        def replace_param(match):
            param_type = match.group('type') or 'string'
            param_name = match.group('name')
            self.param_types[param_name] = param_type
            type_regex = self.TYPE_PATTERNS.get(param_type, r'[^/]+')
            return f'(?P<{param_name}>{type_regex})'

        regex_str = re.sub(pattern, replace_param, path)
        return re.compile(f'^{regex_str}$')

    def match(self, path: str, method: str) -> Optional[Dict[str, Any]]:
        """Validates HTTP method and extracts typed argument dictionary if path matches."""
        if method not in self.methods:
            return None
        match = self.regex.match(path)
        if not match:
            return None

        raw_args = match.groupdict()
        converted_args = {}
        for key, val in raw_args.items():
            param_type = self.param_types.get(key, 'string')
            if param_type == 'int':
                converted_args[key] = int(val)
            elif param_type == 'float':
                converted_args[key] = float(val)
            else:
                converted_args[key] = str(val)

        return converted_args


# =============================================================================
# MAIN NIX FLASK WEB FRAMEWORK ENGINE
# =============================================================================

class NixFlask:
    """
    Core Framework Engine bringing Flask capabilities to the Nix environment.
    Handles route registration, HTTP dispatch, WSGI compliance, HTML rendering, and JSON APIs.
    """
    HTTP_STATUS_CODES = {
        200: "200 OK",
        201: "201 Created",
        302: "302 Found",
        400: "400 Bad Request",
        404: "404 Not Found",
        405: "405 Method Not Allowed",
        500: "500 Internal Server Error"
    }

    def __init__(self, name: str = "NixApp", nix_env: Optional[NixEnvironment] = None):
        self.name = name
        self.routes: List[NixWebRoute] = []
        self.nix_env = nix_env if nix_env is not None else NixEnvironment()
        self.error_handlers: Dict[int, Callable] = {}
        
        # Register core native web functions inside Nix runtime scope
        self._register_nix_web_builtins()

    def _register_nix_web_builtins(self) -> None:
        """Injects native web utilities directly into the Nix environment."""
        self.nix_env.define("web_render", self.render_template)
        self.nix_env.define("web_json", NixWebResponse.json)
        self.nix_env.define("web_html", NixWebResponse.html)
        self.nix_env.define("web_redirect", NixWebResponse.redirect)

    def route(self, path: str, methods: Optional[List[str]] = None):
        """Python Decorator syntax for registering route handlers (like @app.route)."""
        if methods is None:
            methods = ['GET']

        def decorator(handler: Callable):
            route_obj = NixWebRoute(path, methods, handler)
            self.routes.append(route_obj)
            return handler
        return decorator

    def register_nix_handler(self, path: str, nix_func_name: str, methods: Optional[List[str]] = None):
        """Registers a route that points to a function defined inside the Nix Environment."""
        if methods is None:
            methods = ['GET']
        route_obj = NixWebRoute(path, methods, nix_func_name)
        self.routes.append(route_obj)

    def render_template(self, template_str_or_path: str, is_file: bool = False, **context) -> NixWebResponse:
        """Renders HTML template string or file with supplied context and returns NixWebResponse."""
        if is_file:
            content = NixTemplateEngine.render_file(template_str_or_path, context)
        else:
            content = NixTemplateEngine.render_string(template_str_or_path, context)
        return NixWebResponse.html(content)

    def _dispatch_request(self, request: NixWebRequest) -> NixWebResponse:
        """Routes incoming request to the matching Python handler or Nix environment function."""
        path_matched = False

        for r in self.routes:
            if r.regex.match(request.path):
                path_matched = True
                args = r.match(request.path, request.method)
                if args is not None:
                    request.path_params = args
                    try:
                        # 1. Dispatching to Python Callable Handler
                        if callable(r.handler):
                            result = r.handler(request, **args)
                        
                        # 2. Dispatching to Nix Environment Function Handler
                        elif isinstance(r.handler, str) and self.nix_env.is_defined(r.handler):
                            nix_func = self.nix_env.lookup(r.handler)
                            if callable(nix_func):
                                result = nix_func(request.to_nix_dict(), **args)
                            else:
                                result = str(nix_func)
                        else:
                            raise RuntimeError(f"Handler '{r.handler}' could not be resolved.")

                        # Formatting response output types
                        if isinstance(result, NixWebResponse):
                            return result
                        elif isinstance(result, tuple) and len(result) == 2:
                            body, status = result
                            return NixWebResponse(body, status_code=status)
                        elif isinstance(result, (dict, list)):
                            return NixWebResponse.json(result)
                        else:
                            return NixWebResponse.html(str(result))

                    except Exception as exc:
                        print(f"[NixFlask Error]: Exception in route handler ({request.path}): {exc}")
                        return self._handle_error(500, request, exc)

        if path_matched:
            return self._handle_error(405, request)
        return self._handle_error(404, request)

    def _handle_error(self, code: int, request: NixWebRequest, exception: Optional[Exception] = None) -> NixWebResponse:
        """Executes custom status error handler or generates default styled error page."""
        if code in self.error_handlers:
            res = self.error_handlers[code](request, exception)
            if isinstance(res, NixWebResponse):
                return res
            return NixWebResponse(str(res), status_code=code)

        status_msg = self.HTTP_STATUS_CODES.get(code, "Unknown Error")
        default_html = f"""
        <!DOCTYPE html>
        <html lang="en">
        <head>
            <meta charset="UTF-8">
            <title>{code} - {status_msg}</title>
            <style>
                body {{ font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #0f172a; color: #f8fafc; display: flex; height: 100vh; align-items: center; justify-content: center; margin: 0; }}
                .card {{ background: #1e293b; padding: 2.5rem 3.5rem; border-radius: 12px; box-shadow: 0 10px 25px rgba(0,0,0,0.5); text-align: center; border: 1px solid #334155; }}
                h1 {{ font-size: 3.5rem; margin: 0; color: #f43f5e; }}
                p {{ font-size: 1.2rem; color: #94a3b8; margin-top: 0.5rem; }}
                .tag {{ background: #334155; padding: 4px 12px; border-radius: 999px; font-size: 0.85rem; color: #38bdf8; display: inline-block; margin-top: 1.5rem; }}
            </style>
        </head>
        <body>
            <div class="card">
                <h1>HTTP {code}</h1>
                <p>{status_msg}</p>
                <div class="tag">NixFlask Engine v1.0.0</div>
            </div>
        </body>
        </html>
        """
        return NixWebResponse.html(default_html, status_code=code)

    def __call__(self, environ: Dict[str, Any], start_response: Callable) -> List[bytes]:
        """WSGI Standard Entry Point matching WSGI Application Protocol."""
        request = NixWebRequest(environ)
        response = self._dispatch_request(request)
        status_text = self.HTTP_STATUS_CODES.get(response.status_code, f"{response.status_code} Status")
        start_response(status_text, response.headers)
        return [response.body]

    def run(self, host: str = "127.0.0.1", port: int = 5000) -> None:
        """Runs built-in development WSGI HTTP Server."""
        print(f"============================================================")
        print(f" * NixFlask App '{self.name}' running on http://{host}:{port}/")
        print(f" * Connected to Nix Runtime Environment successfully!")
        print(f" * Press CTRL+C to quit")
        print(f"============================================================")
        
        httpd = make_server(host, port, self)
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n * NixFlask Web Server stopped gracefully.")


# =============================================================================
# INTEGRATION DEMONSTRATION & TEST SUITE
# =============================================================================

if __name__ == '__main__':
    # Initialize Nix lexical environment and NixFlask framework
    nix_global_env = NixEnvironment()
    app = NixFlask("NixWebDemo", nix_env=nix_global_env)

    # 1. Defining a function directly inside the Nix Environment (Nix Function Handler)
    def nix_user_profile_handler(req_dict: Dict[str, Any], username: str) -> NixWebResponse:
        """Nix handler receiving request context and path argument."""
        html_code = f"""
        <!DOCTYPE html>
        <html>
        <head>
            <title>Nix Profile - {username}</title>
            <style>
                body {{ font-family: sans-serif; background: #f1f5f9; padding: 2rem; color: #1e293b; }}
                .container {{ max-width: 600px; margin: 0 auto; background: white; padding: 2rem; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }}
                code {{ background: #e2e8f0; padding: 2px 6px; border-radius: 4px; color: #0f172a; }}
            </style>
        </head>
        <body>
            <div class="container">
                <h2>Nix Runtime User Profile</h2>
                <p>Hello <b>{username}</b>! This page was rendered by a handler registered in the Nix Environment.</p>
                <p><b>HTTP Method:</b> <code>{req_dict['method']}</code></p>
                <p><a href="/">Return to Home Page</a></p>
            </div>
        </body>
        </html>
        """
        return NixWebResponse.html(html_code)

    # Register handler in Nix Environment
    nix_global_env.define("handle_nix_profile", nix_user_profile_handler)
    
    # Register dynamic Nix route
    app.register_nix_handler("/nix/user/<string:username>", "handle_nix_profile")

    # 2. Main HTML Home Page Template
    HOME_HTML_TEMPLATE = """
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>{{ title }}</title>
        <style>
            body { font-family: 'Segoe UI', system-ui, sans-serif; margin: 0; padding: 0; background-color: #0f172a; color: #e2e8f0; }
            .header { background: linear-gradient(135deg, #1e293b, #0f172a); border-bottom: 1px solid #334155; padding: 1.5rem; text-align: center; }
            .header h1 { margin: 0; color: #38bdf8; font-size: 2rem; }
            .container { max-width: 800px; margin: 2rem auto; padding: 0 1rem; }
            .card { background: #1e293b; border: 1px solid #334155; border-radius: 8px; padding: 1.5rem; margin-bottom: 1.5rem; }
            h2 { color: #f43f5e; margin-top: 0; }
            a { color: #38bdf8; text-decoration: none; }
            a:hover { text-decoration: underline; }
            input[type="text"] { width: 70%; padding: 0.6rem; border-radius: 4px; border: 1px solid #475569; background: #0f172a; color: white; }
            button { background: #0284c7; color: white; border: none; padding: 0.6rem 1.2rem; border-radius: 4px; cursor: pointer; font-weight: bold; }
            button:hover { background: #0369a1; }
        </style>
    </head>
    <body>
        <div class="header">
            <h1>{{ app_name }}</h1>
            <p>Connecting Nix Language Runtime to HTML & WSGI Web</p>
        </div>
        <div class="container">
            <div class="card">
                <h2>Welcome, {{ username }}!</h2>
                <p>NixFlask allows you to write routes, handlers, and templates directly in Python & Nix.</p>
                <h3>Available Routes:</h3>
                <ul>
                    <li><a href="/">Home Page (GET)</a></li>
                    <li><a href="/nix/user/Alice">Nix Dynamic Route (/nix/user/Alice)</a></li>
                    <li><a href="/api/info">Nix JSON API Endpoint (/api/info)</a></li>
                </ul>
            </div>

            <div class="card">
                <h2>Test POST Form Submission</h2>
                <form action="/submit" method="POST">
                    <input type="text" name="message" value="Hello NixFlask Web Framework!" required />
                    <button type="submit">Send POST</button>
                </form>
            </div>
        </div>
    </body>
    </html>
    """

    # Route: Home Page
    @app.route('/')
    def index(req: NixWebRequest):
        return app.render_template(
            HOME_HTML_TEMPLATE,
            title="NixFlask Framework",
            app_name="NixFlask Web Framework Engine",
            username="Nix Developer"
        )

    # Route: JSON API Endpoint
    @app.route('/api/info')
    def api_info(req: NixWebRequest):
        return {
            "status": "success",
            "framework": "NixFlask",
            "language_engine": "Nix Language Bridge",
            "features": [
                "WSGI Server Compliance",
                "Nix Environment Binding",
                "Dynamic Flask Routes (<type:param>)",
                "HTML Template Rendering",
                "JSON APIs & POST Forms"
            ]
        }

    # Route: Form POST submission
    @app.route('/submit', methods=['POST'])
    def handle_submit(req: NixWebRequest):
        submitted_msg = req.get_form_field('message', 'No message sent')
        html = f"""
        <!DOCTYPE html>
        <html>
        <body style="font-family: sans-serif; background: #0f172a; color: white; padding: 3rem;">
            <h2 style="color: #4ade80;">Form Received Successfully!</h2>
            <p><b>Submitted Message:</b> <code>{submitted_msg}</code></p>
            <p><a href="/" style="color: #38bdf8;">Back to Home</a></p>
        </body>
        </html>
        """
        return NixWebResponse.html(html)

    # Launch local WSGI development server on http://127.0.0.1:5000
    app.run(host="127.0.0.1", port=5000)

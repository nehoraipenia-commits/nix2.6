import sys

class TokenType:
    """
    Defines all token types supported by the Nix programming language.
    Includes customized keywords matching the C++/Python-like hybrid syntax.
    """
    # Literals and Identifiers
    IDENTIFIER = 'IDENTIFIER'
    NUMBER = 'NUMBER'
    STRING = 'STRING'
    
    # Keywords
    FCT = 'FCT'          # replaces 'def'
    IF = 'IF'
    ELSE = 'ELSE'
    WHILE = 'WHILE'
    FOR = 'FOR'
    OUT = 'OUT'          # replaces 'print'
    CIN = 'CIN'          # replaces 'input'
    USING = 'USING'      # replaces 'import'
    TRUE = 'TRUE'        # true (lowercase)
    FALSE = 'FALSE'      # false (lowercase)
    RETURN = 'RETURN'
    CLASS = 'CLASS'
    
    # Operators and Mathematical Symbols
    ASSIGN = 'ASSIGN'
    PLUS = 'PLUS'
    MINUS = 'MINUS'
    MUL = 'MUL'
    DIV = 'DIV'
    MOD = 'MOD'
    
    # Comparison Operators
    EQ = 'EQ'            # ==
    NE = 'NE'            # !=
    LT = 'LT'            # <
    GT = 'GT'            # >
    LTE = 'LTE'          # <=
    GTE = 'GTE'          # >=
    
    # Delimiters and Brackets
    LPAREN = 'LPAREN'    # (
    RPAREN = 'RPAREN'    # )
    LBRACE = 'LBRACE'    # {
    RBRACE = 'RBRACE'    # }
    COMMA = 'COMMA'      # ,
    LBRACK = 'LBRACK'  # [
    RBRACK = 'RBRACK'  # ]
    COLON = 'COLON' # :
    
    # Bracket wrappers for imports (using <lib>)
    LANGLE = 'LANGLE'    # <
    RANGLE = 'RANGLE'    # >
    DOT = 'DOT' # .
    
    EOF = 'EOF'          # End of file marker


class Token:
    """
    Represents a single lexical unit (Token) with its type, value, and position in source.
    """
    def __init__(self, type_, value, line, column):
        self.type = type_
        self.value = value
        self.line = line
        self.column = column

    def __repr__(self):
        return f"Token({self.type}, {repr(self.value)}, line={self.line}, col={self.column})"


class Lexer:
    """
    Lexer for the Nix programming language.
    Converts raw Nix source code into a stream of structured Tokens.
    """
    def __init__(self, source_code):
        self.source = source_code
        self.position = 0
        self.line = 1
        self.column = 1

    def error(self, message):
        """Raises a structured syntax error pointing to the exact line and column."""
        raise SyntaxError(f"Nix Lexer Error: {message} at line {self.line}, column {self.column}")

    def peek(self):
        """Returns the current character without consuming it."""
        if self.position >= len(self.source):
            return None
        return self.source[self.position]

    def advance(self):
        """Consumes and returns the current character, moving the cursor forward."""
        char = self.peek()
        if char:
            self.position += 1
            if char == '\n':
                self.line += 1
                self.column = 1
            else:
                self.column += 1
        return char

    def tokenize(self):
        """Main entry point to scan the source code and generate a list of Tokens."""
        tokens = []
        
        # Mapping keywords to token types (including C++ style 'using' and custom 'fct')
        keywords = {
            'fct': TokenType.FCT,
            'class': TokenType.CLASS,
            'if': TokenType.IF,
            'else': TokenType.ELSE,
            'while': TokenType.WHILE,
            'for': TokenType.FOR,
            'out': TokenType.OUT,
            'cin': TokenType.CIN,
            'using': TokenType.USING,
            'true': TokenType.TRUE,
            'false': TokenType.FALSE,
            'return': TokenType.RETURN
        }

        while self.peek() is not None:
            char = self.peek()

            # Skip spaces, tabs, and carriage returns
            if char.isspace():
                self.advance()
                continue

            # Skip single-line comments (both '#' python style and '//' C++ style)
            if char == '#':
                while self.peek() is not None and self.peek() != '\n':
                    self.advance()
                continue
            if char == '/' and self.position + 1 < len(self.source) and self.source[self.position + 1] == '/':
                while self.peek() is not None and self.peek() != '\n':
                    self.advance()
                continue

            # Numeric literals (integers and floating-points)
            if char.isdigit():
                num_str = ""
                start_col = self.column
                while self.peek() is not None and (self.peek().isdigit() or self.peek() == '.'):
                    num_str += self.advance()
                if '.' in num_str:
                    tokens.append(Token(TokenType.NUMBER, float(num_str), self.line, start_col))
                else:
                    tokens.append(Token(TokenType.NUMBER, int(num_str), self.line, start_col))
                continue

            # Identifiers and Nix keywords
            if char.isalpha() or char == '_':
                ident_str = ""
                start_col = self.column
                while self.peek() is not None and (self.peek().isalnum() or self.peek() == '_'):
                    ident_str += self.advance()
                
                if ident_str in keywords:
                    tokens.append(Token(keywords[ident_str], ident_str, self.line, start_col))
                else:
                    tokens.append(Token(TokenType.IDENTIFIER, ident_str, self.line, start_col))
                continue

            # String literals (supports single and double quotes)
            if char == '"' or char == "'":
                quote_type = self.advance()
                string_str = ""
                start_col = self.column
                while self.peek() is not None and self.peek() != quote_type:
                    string_str += self.advance()
                if self.peek() is None:
                    self.error("Unterminated string literal")
                self.advance()  # Consume the closing quote
                tokens.append(Token(TokenType.STRING, string_str, self.line, start_col))
                continue

            # Multi-character Operators (==, !=, <=, >=, etc.)
            if char == '=':
                start_col = self.column
                self.advance()
                if self.peek() == '=':
                    self.advance()
                    tokens.append(Token(TokenType.EQ, '==', self.line, start_col))
                else:
                    tokens.append(Token(TokenType.ASSIGN, '=', self.line, start_col))
                continue

            if char == '!':
                start_col = self.column
                self.advance()
                if self.peek() == '=':
                    self.advance()
                    tokens.append(Token(TokenType.NE, '!=', self.line, start_col))
                else:
                    self.error("Unexpected character '!' without '='")
                continue

            if char == '<':
                start_col = self.column
                self.advance()
                if self.peek() == '=':
                    self.advance()
                    tokens.append(Token(TokenType.LTE, '<=', self.line, start_col))
                else:
                    tokens.append(Token(TokenType.LANGLE, '<', self.line, start_col))
                continue

            if char == '>':
                start_col = self.column
                self.advance()
                if self.peek() == '=':
                    self.advance()
                    tokens.append(Token(TokenType.GTE, '>=', self.line, start_col))
                else:
                    tokens.append(Token(TokenType.RANGLE, '>', self.line, start_col))
                continue

            # Single character operators and structural delimiters
            single_chars = {
                '+': TokenType.PLUS,
                '-': TokenType.MINUS,
                '*': TokenType.MUL,
                '/': TokenType.DIV,
                '%': TokenType.MOD,
                '(': TokenType.LPAREN,
                ')': TokenType.RPAREN,
                '{': TokenType.LBRACE,
                '}': TokenType.RBRACE,
                ',': TokenType.COMMA,
                '[': TokenType.LBRACK,  
                ']': TokenType.RBRACK,
                ':': TokenType.COLON,
                '.': TokenType.DOT,
            }

            if char in single_chars:
                tokens.append(Token(single_chars[char], char, self.line, self.column))
                self.advance()
                continue

            self.error(f"Unexpected character: {repr(char)}")

        # Append End-Of-File marker
        tokens.append(Token(TokenType.EOF, None, self.line, self.column))
        return tokens

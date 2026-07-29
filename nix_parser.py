from lexer import TokenType, Token
from ast_nodes import (
    ProgramNode, VarAssignNode, FuncDefNode, IfStmtNode, 
    WhileLoopNode, ForLoopNode, ReturnNode, OutNode, 
    BinOpNode, UnaryOpNode, LiteralNode, IdentifierNode, 
    FuncCallNode, CinCallNode, VectorNode, DictionaryNode, SubscriptNode,
    UsingNode, ClassDefNode, GetAttrNode # Added here to resolve the NameError!
)

class Parser:
    """
    Parser for the Nix programming language.
    Translates a sequence of Tokens from the Lexer into an Abstract Syntax Tree (AST).
    Resolves statement blocks, functions, variables, expressions, and custom loops.
    """
    def __init__(self, tokens):
        self.tokens = tokens
        self.position = 0

    def error(self, message):
        """Raises a structured compilation syntax error with positional details."""
        token = self.current_token()
        raise SyntaxError(
            f"Nix Parser Error: {message} at token {token.type} ({repr(token.value)}) "
            f"on line {token.line}, column {token.column}"
        )

    def current_token(self):
        """Returns the token at the current parsing pointer."""
        return self.tokens[self.position]

    def advance(self):
        """Consumes the current token and moves the parser position forward."""
        token = self.current_token()
        if token.type != TokenType.EOF:
            self.position += 1
        return token

    def check(self, type_):
        """Checks if the current token matches a specific type without consuming it."""
        return self.current_token().type == type_

    def match(self, *types):
        """Consumes the current token if it matches any of the specified token types."""
        for type_ in types:
            if self.check(type_):
                self.advance()
                return True
        return False

    def consume(self, type_, error_message):
        """Asserts that the current token is of the expected type, then consumes it."""
        if self.check(type_):
            return self.advance()
        self.error(error_message)

    # --------------------------------------------------------------------------
    # Language Grammar Rules
    # --------------------------------------------------------------------------

    def parse(self):
        """Main parser loop. Parses the token list until EOF is encountered."""
        statements = []
        while not self.check(TokenType.EOF):
            stmt = self.statement()
            if stmt:
                statements.append(stmt)
        return ProgramNode(statements)

    def statement(self):
        """Routes statement parsing based on the leading keyword."""
        if self.match(TokenType.USING):
            return self.using_statement()
        if self.match(TokenType.CLASS):       
            return self.class_definition()
        if self.match(TokenType.FCT):
            return self.func_definition()
        if self.match(TokenType.IF):
            return self.if_statement()
        if self.match(TokenType.WHILE):
            return self.while_loop()
        if self.match(TokenType.FOR):
            return self.for_loop()
        if self.match(TokenType.RETURN):
            return self.return_statement()
        if self.match(TokenType.OUT):
            return self.out_statement()
        
        # Default fallback to an expression statement (like assignment or calling a fct)
        return self.expression_statement()

    def using_statement(self):
        """Parses modular import statements: using <library_name>"""
        self.consume(TokenType.LANGLE, "Expected '<' after 'using'")
        lib_token = self.consume(TokenType.IDENTIFIER, "Expected module name inside '<>'")
        self.consume(TokenType.RANGLE, "Expected '>' after module name")
        return UsingNode(lib_token.value)

    def func_definition(self):
        """Parses function declaration: fct name(args) { body }"""
        name_token = self.consume(TokenType.IDENTIFIER, "Expected function name after 'fct'")
        self.consume(TokenType.LPAREN, "Expected '(' after function name")
        
        params = []
        if not self.check(TokenType.RPAREN):
            params.append(self.consume(TokenType.IDENTIFIER, "Expected parameter name").value)
            while self.match(TokenType.COMMA):
                params.append(self.consume(TokenType.IDENTIFIER, "Expected parameter name").value)
                
        self.consume(TokenType.RPAREN, "Expected ')' after parameter list")
        body_block = self.block()
        return FuncDefNode(name_token.value, params, body_block)

    def block(self):
        """Parses sequential statement blocks closed by braces: { statements }"""
        self.consume(TokenType.LBRACE, "Expected '{' to start block")
        statements = []
        while not self.check(TokenType.RBRACE) and not self.check(TokenType.EOF):
            statements.append(self.statement())
        self.consume(TokenType.RBRACE, "Expected '}' to close block")
        return ProgramNode(statements)

    def if_statement(self):
        """Parses: if condition { body } else { body }"""
        condition = self.expression()
        then_branch = self.block()
        else_branch = None
        
        if self.match(TokenType.ELSE):
            if self.check(TokenType.IF):
                self.advance()
                else_branch = self.if_statement()
            else:
                else_branch = self.block()
                
        return IfStmtNode(condition, then_branch, else_branch)

    def while_loop(self):
        """Parses: while condition { body }"""
        condition = self.expression()
        body_block = self.block()
        return WhileLoopNode(condition, body_block)

    def for_loop(self):
        """Parses Nix custom loop format: for iterator * limit_expr { body }"""
        iterator_token = self.consume(TokenType.IDENTIFIER, "Expected loop iterator variable name")
        self.consume(TokenType.MUL, "Expected '*' operator after iterator (e.g., 'for i * 10')")
        limit_expr = self.expression()
        body_block = self.block()
        return ForLoopNode(iterator_token.value, limit_expr, body_block)

    def return_statement(self):
        """Parses: return expression"""
        expr = None
        if not self.check(TokenType.RBRACE) and not self.check(TokenType.EOF):
            expr = self.expression()
        return ReturnNode(expr)

    def out_statement(self):
        """Parses console output commands: out(expr1, expr2)"""
        self.consume(TokenType.LPAREN, "Expected '(' after 'out'")
        arguments = []
        if not self.check(TokenType.RPAREN):
            arguments.append(self.expression())
            while self.match(TokenType.COMMA):
                arguments.append(self.expression())
        self.consume(TokenType.RPAREN, "Expected ')' after output arguments")
        return OutNode(arguments)

    def expression_statement(self):
        """Parses variable assignment (target = value) or direct expression evaluation."""
        expr = self.expression()
        if self.match(TokenType.ASSIGN):
            value_expr = self.expression()
            # On autorise les variables, les index de tableaux ET les propriétés d'objets (GetAttrNode)
            if isinstance(expr, (IdentifierNode, SubscriptNode, GetAttrNode)):
                return VarAssignNode(expr, value_expr)
            self.error("Invalid assignment target. Left-hand side must be a variable, index subscript, or property.")
        return expr

    # --------------------------------------------------------------------------
    # Expression Order of Operations and Precedence
    # --------------------------------------------------------------------------

    def expression(self):
        return self.comparison()

    def comparison(self):
        """Resolves ordering comparisons: less-than, greater-than, and boundary inclusive checks."""
        left = self.additive()
        while self.match(TokenType.EQ, TokenType.NE, TokenType.LANGLE, TokenType.RANGLE, TokenType.LTE, TokenType.GTE):
            op = self.tokens[self.position - 1].value
            right = self.additive()
            left = BinOpNode(left, op, right)
        return left

    def additive(self):
        left = self.multiplicative()
        while self.match(TokenType.PLUS, TokenType.MINUS):
            op = self.tokens[self.position - 1].value
            right = self.multiplicative()
            left = BinOpNode(left, op, right)
        return left

    def multiplicative(self):
        left = self.unary()
        while self.match(TokenType.MUL, TokenType.DIV, TokenType.MOD):
            op = self.tokens[self.position - 1].value
            right = self.unary()
            left = BinOpNode(left, op, right)
        return left

    def unary(self):
        if self.match(TokenType.MINUS):
            return UnaryOpNode('-', self.unary())
        return self.call_or_subscript()

    def call_or_subscript(self):
        """Handles post-fix operations like function invocations, index subscripts, and property access."""
        expr = self.primary()
        
        while True:
            # 1. Appels de fonctions / méthodes : expr(args)
            if self.match(TokenType.LPAREN):
                arguments = []
                if not self.check(TokenType.RPAREN):
                    arguments.append(self.expression())
                    while self.match(TokenType.COMMA):
                        arguments.append(self.expression())
                self.consume(TokenType.RPAREN, "Expected ')' after function arguments")
                expr = FuncCallNode(expr, arguments)
            
            # 2. Accès par index : expr[index]
            elif self.match(TokenType.LBRACK):
                index_expr = self.expression()
                self.consume(TokenType.RBRACK, "Expected ']' after subscript index expression")
                expr = SubscriptNode(expr, index_expr)

            # 3. Accès aux propriétés / méthodes : expr.propriete
            elif self.match(TokenType.DOT):
                member_token = self.consume(TokenType.IDENTIFIER, "Expected property name after '.'")
                expr = GetAttrNode(expr, member_token.value)
                
            else:
                break
                
        return expr

    def class_definition(self):
        """Parses class declaration: class Name { body }"""
        name_token = self.consume(TokenType.IDENTIFIER, "Expected class name after 'class'")
        body_block = self.block()
        return ClassDefNode(name_token.value, body_block)

    def primary(self):
        """Resolves basic primitive data values, identifiers, and braced groupings."""
        if self.match(TokenType.NUMBER, TokenType.STRING):
            return LiteralNode(self.tokens[self.position - 1].value)
            
        if self.match(TokenType.TRUE):
            return LiteralNode(True)
            
        if self.match(TokenType.FALSE):
            return LiteralNode(False)
            
        if self.match(TokenType.CIN):
            self.consume(TokenType.LPAREN, "Expected '(' after 'cin'")
            
            prompt_expr = None
            
            if self.current_token().type != TokenType.RPAREN:
                prompt_expr = self.expression()
                
            self.consume(TokenType.RPAREN, "Expected ')' after 'cin'")
            return CinCallNode(prompt_expr)

        if self.match(TokenType.IDENTIFIER):
            return IdentifierNode(self.tokens[self.position - 1].value)

        if self.match(TokenType.LPAREN):
            expr = self.expression()
            self.consume(TokenType.RPAREN, "Expected ')' after grouped expression")
            return expr

        if self.match(TokenType.LBRACK):
            elements = []
            if self.current_token().type != TokenType.RBRACK:
                elements.append(self.expression())
                while self.match(TokenType.COMMA):
                    elements.append(self.expression())
            self.consume(TokenType.RBRACK, "Expected ']' at the end of vector")
            return VectorNode(elements)

        if self.match(TokenType.LBRACE):
            pairs = {}
            if self.current_token().type != TokenType.RBRACE:
                key = self.expression()
                self.consume(TokenType.COLON, "Expected ':' after dictionary key")
                value = self.expression()
                pairs[key] = value
                while self.match(TokenType.COMMA):
                    key = self.expression()
                    self.consume(TokenType.COLON, "Expected ':' after dictionary key")
                    value = self.expression()
                    pairs[key] = value
            self.consume(TokenType.RBRACE, "Expected '}' at the end of dictionary literal")
            return DictionaryNode(pairs)

        self.error("Expected expression or value representation")

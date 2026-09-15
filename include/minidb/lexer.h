// lexer.h
// -----------------------------------------------------------------------------
// The lexer turns a raw SQL-like string into a flat list of tokens. It knows
// nothing about grammar - only about characters. This keeps the parser clean:
// it consumes tokens instead of re-scanning characters.
// -----------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

namespace minidb {

enum class TokenType {
    Keyword,      // CREATE, SELECT, FROM, ... (case-insensitive)
    Identifier,   // table / column / database names
    IntLiteral,   // 42
    FloatLiteral, // 3.14
    StringLiteral,// 'hello'
    Operator,     // = != < <= > >=
    Symbol,       // ( ) , ; * .
    End           // end-of-input sentinel
};

struct Token {
    TokenType   type = TokenType::End;
    std::string text;      // original text (keywords stored upper-cased)
    int         pos = 0;   // character offset, for error messages

    bool is(TokenType t) const { return type == t; }
    bool isKeyword(const char* kw) const;   // case-insensitive keyword match
    bool isSymbol(char c) const;
};

class Lexer {
public:
    explicit Lexer(std::string src);

    // Tokenize the whole input. Throws DBError on an illegal character or an
    // unterminated string literal.
    std::vector<Token> tokenize();

private:
    std::string src_;
    std::size_t pos_ = 0;

    char peek() const;
    char peek2() const;
    char advance();
    bool eof() const { return pos_ >= src_.size(); }
    void skipWhitespaceAndComments();

    Token lexNumber();
    Token lexString();
    Token lexWord();      // identifier or keyword
    Token lexOperatorOrSymbol();
};

// Set of reserved keywords. Exposed so the parser can double-check if needed.
bool isKeyword(const std::string& upper);

} // namespace minidb

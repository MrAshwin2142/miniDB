// lexer.cpp
#include "minidb/lexer.h"
#include "minidb/common.h"
#include "minidb/utils.h"

#include <cctype>
#include <unordered_set>

namespace minidb {

// The reserved-word table. Anything not in here that looks like a word is an
// identifier. Keywords are matched case-insensitively.
static const std::unordered_set<std::string>& keywords() {
    static const std::unordered_set<std::string> kw = {
        "CREATE", "DROP", "DATABASE", "DATABASES", "TABLE", "TABLES", "USE",
        "SHOW", "DESCRIBE", "DESC", "INDEX", "INDEXES", "ON",
        "INSERT", "INTO", "VALUES", "SELECT", "FROM", "WHERE", "UPDATE",
        "SET", "DELETE", "PRIMARY", "KEY",
        "INT", "INTEGER", "FLOAT", "DOUBLE", "TEXT", "STRING", "CHAR", "VARCHAR",
        "AND", "ORDER", "BY", "ASC", "DESC", "LIMIT",
        "COUNT", "SUM", "AVG", "MIN", "MAX",
        "EXPLAIN", "CATALOG", "STATS", "HELP", "TRACE", "OFF", "EXEC",
        "EXIT", "QUIT", "NULL", "NOT"
    };
    return kw;
}

bool isKeyword(const std::string& upper) {
    return keywords().count(upper) > 0;
}

bool Token::isKeyword(const char* kw) const {
    return type == TokenType::Keyword && util::iequals(text, kw);
}

bool Token::isSymbol(char c) const {
    return type == TokenType::Symbol && text.size() == 1 && text[0] == c;
}

Lexer::Lexer(std::string src) : src_(std::move(src)) {}

char Lexer::peek() const  { return eof() ? '\0' : src_[pos_]; }
char Lexer::peek2() const { return (pos_ + 1 >= src_.size()) ? '\0' : src_[pos_ + 1]; }
char Lexer::advance()     { return src_[pos_++]; }

void Lexer::skipWhitespaceAndComments() {
    for (;;) {
        while (!eof() && std::isspace(static_cast<unsigned char>(peek()))) advance();
        // SQL line comment: -- to end of line
        if (peek() == '-' && peek2() == '-') {
            while (!eof() && peek() != '\n') advance();
            continue;
        }
        break;
    }
}

Token Lexer::lexNumber() {
    int start = static_cast<int>(pos_);
    std::string num;
    bool isFloat = false;
    while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) num += advance();
    if (peek() == '.') {
        isFloat = true;
        num += advance();
        while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) num += advance();
    }
    Token t;
    t.type = isFloat ? TokenType::FloatLiteral : TokenType::IntLiteral;
    t.text = num;
    t.pos = start;
    return t;
}

Token Lexer::lexString() {
    int start = static_cast<int>(pos_);
    advance(); // opening quote
    std::string val;
    while (!eof() && peek() != '\'') {
        char c = advance();
        // Support '' as an escaped single quote inside a literal.
        if (c == '\\' && !eof()) { val += advance(); continue; }
        val += c;
    }
    if (eof()) throw DBError("unterminated string literal");
    advance(); // closing quote
    Token t;
    t.type = TokenType::StringLiteral;
    t.text = val;
    t.pos = start;
    return t;
}

Token Lexer::lexWord() {
    int start = static_cast<int>(pos_);
    std::string word;
    while (!eof() &&
           (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
        word += advance();
    }
    Token t;
    t.pos = start;
    std::string upper = util::toUpper(word);
    if (isKeyword(upper)) {
        t.type = TokenType::Keyword;
        t.text = upper;            // store keywords normalized to upper-case
    } else {
        t.type = TokenType::Identifier;
        t.text = word;             // identifiers keep their original casing
    }
    return t;
}

Token Lexer::lexOperatorOrSymbol() {
    int start = static_cast<int>(pos_);
    char c = advance();
    Token t;
    t.pos = start;

    switch (c) {
        case '<':
            if (peek() == '=') { advance(); t.type = TokenType::Operator; t.text = "<="; }
            else if (peek() == '>') { advance(); t.type = TokenType::Operator; t.text = "!="; }
            else { t.type = TokenType::Operator; t.text = "<"; }
            return t;
        case '>':
            if (peek() == '=') { advance(); t.type = TokenType::Operator; t.text = ">="; }
            else { t.type = TokenType::Operator; t.text = ">"; }
            return t;
        case '=':
            t.type = TokenType::Operator; t.text = "=";
            return t;
        case '!':
            if (peek() == '=') { advance(); t.type = TokenType::Operator; t.text = "!="; return t; }
            throw DBError(std::string("unexpected character '!'"));
        case '(': case ')': case ',': case ';': case '*': case '.':
        case '-': case '+':
            t.type = TokenType::Symbol; t.text = std::string(1, c);
            return t;
        default:
            throw DBError(std::string("unexpected character '") + c + "'");
    }
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    for (;;) {
        skipWhitespaceAndComments();
        if (eof()) break;
        char c = peek();
        if (std::isdigit(static_cast<unsigned char>(c))) {
            out.push_back(lexNumber());
        } else if (c == '\'') {
            out.push_back(lexString());
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            out.push_back(lexWord());
        } else {
            out.push_back(lexOperatorOrSymbol());
        }
    }
    Token end;
    end.type = TokenType::End;
    end.pos = static_cast<int>(src_.size());
    out.push_back(end);
    return out;
}

} // namespace minidb

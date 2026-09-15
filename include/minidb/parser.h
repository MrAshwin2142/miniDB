// parser.h
// -----------------------------------------------------------------------------
// A hand-written recursive-descent parser. It consumes the token stream from
// the lexer and builds a Statement (see ast.h). Every supported command has its
// own parse function, which makes the grammar easy to read and extend.
// -----------------------------------------------------------------------------
#pragma once

#include "minidb/ast.h"
#include "minidb/lexer.h"

#include <vector>

namespace minidb {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    // Parse exactly one statement (the caller has already split on ';').
    // Throws DBError with a readable message on any syntax error.
    StmtPtr parseStatement();

private:
    std::vector<Token> tokens_;
    std::size_t        pos_ = 0;

    // --- token cursor helpers ---
    const Token& peek() const;
    const Token& peek(std::size_t ahead) const;
    const Token& advance();
    bool         match(TokenType type);
    bool         matchKeyword(const char* kw);
    bool         matchSymbol(char c);
    void         expectSymbol(char c);
    void         expectKeyword(const char* kw);
    std::string  expectIdentifier(const char* what);
    [[noreturn]] void fail(const std::string& msg) const;

    // --- value / clause helpers ---
    Value                  parseLiteral();
    Column                 parseColumnDef();
    Condition              parseCondition();
    std::vector<Condition> parseWhere();       // returns empty if no WHERE

    // --- one parse function per statement kind ---
    StmtPtr parseCreate();
    StmtPtr parseDrop();
    StmtPtr parseUse();
    StmtPtr parseShow();
    StmtPtr parseDescribe();
    StmtPtr parseInsert();
    StmtPtr parseSelect();
    StmtPtr parseUpdate();
    StmtPtr parseDelete();
    StmtPtr parseExplain();
    StmtPtr parseTrace();
    StmtPtr parseExec();
};

} // namespace minidb

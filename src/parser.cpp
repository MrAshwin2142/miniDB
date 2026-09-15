// parser.cpp
#include "minidb/parser.h"
#include "minidb/common.h"
#include "minidb/utils.h"

namespace minidb {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

// ---------------------------------------------------------------------------
// Cursor helpers
// ---------------------------------------------------------------------------
const Token& Parser::peek() const { return tokens_[pos_]; }

const Token& Parser::peek(std::size_t ahead) const {
    std::size_t idx = pos_ + ahead;
    if (idx >= tokens_.size()) return tokens_.back(); // End sentinel
    return tokens_[idx];
}

const Token& Parser::advance() {
    const Token& t = tokens_[pos_];
    if (pos_ + 1 < tokens_.size()) ++pos_;
    return t;
}

bool Parser::match(TokenType type) {
    if (peek().type == type) { advance(); return true; }
    return false;
}

bool Parser::matchKeyword(const char* kw) {
    if (peek().isKeyword(kw)) { advance(); return true; }
    return false;
}

bool Parser::matchSymbol(char c) {
    if (peek().isSymbol(c)) { advance(); return true; }
    return false;
}

void Parser::expectSymbol(char c) {
    if (!matchSymbol(c)) fail(std::string("expected '") + c + "'");
}

void Parser::expectKeyword(const char* kw) {
    if (!matchKeyword(kw)) fail(std::string("expected keyword ") + kw);
}

std::string Parser::expectIdentifier(const char* what) {
    if (peek().type != TokenType::Identifier)
        fail(std::string("expected ") + what);
    return advance().text;
}

void Parser::fail(const std::string& msg) const {
    const Token& t = peek();
    std::string got = (t.type == TokenType::End) ? "end of input" : ("'" + t.text + "'");
    throw DBError("syntax error: " + msg + " (got " + got + ")");
}

// ---------------------------------------------------------------------------
// Literals and shared clauses
// ---------------------------------------------------------------------------
Value Parser::parseLiteral() {
    // Optional unary sign for numbers.
    int sign = 1;
    if (peek().isSymbol('-')) { advance(); sign = -1; }
    else if (peek().isSymbol('+')) { advance(); }

    const Token& t = peek();
    if (t.type == TokenType::IntLiteral) {
        advance();
        return Value::makeInt(sign * std::stoll(t.text));
    }
    if (t.type == TokenType::FloatLiteral) {
        advance();
        return Value::makeFloat(sign * std::stod(t.text));
    }
    if (sign != 1) fail("expected a number after sign");
    if (t.type == TokenType::StringLiteral) {
        advance();
        return Value::makeText(t.text);
    }
    if (t.isKeyword("NULL")) {
        advance();
        return Value::makeNull(ColumnType::INT); // type refined against schema later
    }
    fail("expected a literal value");
}

Column Parser::parseColumnDef() {
    Column col;
    col.name = expectIdentifier("column name");

    const Token& tk = peek();
    if (tk.isKeyword("INT") || tk.isKeyword("INTEGER")) {
        advance(); col.type = ColumnType::INT;
    } else if (tk.isKeyword("FLOAT") || tk.isKeyword("DOUBLE")) {
        advance(); col.type = ColumnType::FLOAT;
    } else if (tk.isKeyword("TEXT") || tk.isKeyword("STRING")) {
        advance(); col.type = ColumnType::TEXT;
    } else if (tk.isKeyword("VARCHAR")) {
        advance(); col.type = ColumnType::TEXT;
        // Accept and ignore an optional length: VARCHAR(255)
        if (matchSymbol('(')) {
            if (peek().type != TokenType::IntLiteral) fail("expected length in VARCHAR(n)");
            advance();
            expectSymbol(')');
        }
    } else if (tk.isKeyword("CHAR")) {
        advance(); col.type = ColumnType::CHAR;
        col.charLen = 1;
        if (matchSymbol('(')) {
            if (peek().type != TokenType::IntLiteral) fail("expected length in CHAR(n)");
            col.charLen = std::stoi(advance().text);
            if (col.charLen <= 0) fail("CHAR length must be positive");
            expectSymbol(')');
        }
    } else {
        fail("expected a column type (INT, FLOAT, TEXT, CHAR)");
    }

    // Optional: PRIMARY KEY
    if (matchKeyword("PRIMARY")) {
        expectKeyword("KEY");
        col.primaryKey = true;
    }
    return col;
}

Condition Parser::parseCondition() {
    Condition c;
    c.column = expectIdentifier("column name in WHERE");
    if (peek().type != TokenType::Operator)
        fail("expected a comparison operator (=, !=, <, <=, >, >=)");
    c.op = advance().text;
    c.value = parseLiteral();
    return c;
}

std::vector<Condition> Parser::parseWhere() {
    std::vector<Condition> conds;
    if (matchKeyword("WHERE")) {
        conds.push_back(parseCondition());
        while (matchKeyword("AND")) conds.push_back(parseCondition());
    }
    return conds;
}

// ---------------------------------------------------------------------------
// Statement dispatch
// ---------------------------------------------------------------------------
StmtPtr Parser::parseStatement() {
    // Tolerate a leading/stray semicolon.
    while (matchSymbol(';')) {}
    const Token& t = peek();

    if (t.type == TokenType::End)
        throw DBError("empty statement");

    if (t.isKeyword("CREATE"))   return parseCreate();
    if (t.isKeyword("DROP"))     return parseDrop();
    if (t.isKeyword("USE"))      return parseUse();
    if (t.isKeyword("SHOW"))     return parseShow();
    if (t.isKeyword("DESCRIBE") || t.isKeyword("DESC")) return parseDescribe();
    if (t.isKeyword("INSERT"))   return parseInsert();
    if (t.isKeyword("SELECT"))   return parseSelect();
    if (t.isKeyword("UPDATE"))   return parseUpdate();
    if (t.isKeyword("DELETE"))   return parseDelete();
    if (t.isKeyword("EXPLAIN"))  return parseExplain();
    if (t.isKeyword("TRACE"))    return parseTrace();
    if (t.isKeyword("EXEC"))     return parseExec();

    if (t.isKeyword("STATS")) { advance(); return std::make_unique<StatsStmt>(); }
    if (t.isKeyword("HELP"))  { advance(); return std::make_unique<HelpStmt>();  }
    if (t.isKeyword("EXIT") || t.isKeyword("QUIT")) { advance(); return std::make_unique<ExitStmt>(); }

    fail("unrecognized statement");
}

// ---- CREATE ----------------------------------------------------------------
StmtPtr Parser::parseCreate() {
    expectKeyword("CREATE");
    if (matchKeyword("DATABASE")) {
        auto s = std::make_unique<CreateDatabaseStmt>();
        s->name = expectIdentifier("database name");
        return s;
    }
    if (matchKeyword("TABLE")) {
        auto s = std::make_unique<CreateTableStmt>();
        s->name = expectIdentifier("table name");
        expectSymbol('(');
        s->columns.push_back(parseColumnDef());
        while (matchSymbol(',')) s->columns.push_back(parseColumnDef());
        expectSymbol(')');
        return s;
    }
    if (matchKeyword("INDEX")) {
        auto s = std::make_unique<CreateIndexStmt>();
        s->indexName = expectIdentifier("index name");
        expectKeyword("ON");
        s->table = expectIdentifier("table name");
        expectSymbol('(');
        s->column = expectIdentifier("column name");
        expectSymbol(')');
        return s;
    }
    fail("expected DATABASE, TABLE or INDEX after CREATE");
}

// ---- DROP ------------------------------------------------------------------
StmtPtr Parser::parseDrop() {
    expectKeyword("DROP");
    if (matchKeyword("DATABASE")) {
        auto s = std::make_unique<DropDatabaseStmt>();
        s->name = expectIdentifier("database name");
        return s;
    }
    if (matchKeyword("TABLE")) {
        auto s = std::make_unique<DropTableStmt>();
        s->name = expectIdentifier("table name");
        return s;
    }
    if (matchKeyword("INDEX")) {
        auto s = std::make_unique<DropIndexStmt>();
        s->indexName = expectIdentifier("index name");
        return s;
    }
    fail("expected DATABASE, TABLE or INDEX after DROP");
}

// ---- USE -------------------------------------------------------------------
StmtPtr Parser::parseUse() {
    expectKeyword("USE");
    auto s = std::make_unique<UseDatabaseStmt>();
    s->name = expectIdentifier("database name");
    return s;
}

// ---- SHOW ------------------------------------------------------------------
StmtPtr Parser::parseShow() {
    expectKeyword("SHOW");
    auto s = std::make_unique<ShowStmt>();
    if (matchKeyword("DATABASES"))    s->what = ShowKind::Databases;
    else if (matchKeyword("TABLES"))  s->what = ShowKind::Tables;
    else if (matchKeyword("CATALOG")) s->what = ShowKind::Catalog;
    else if (matchKeyword("INDEXES") || matchKeyword("INDEX")) s->what = ShowKind::Indexes;
    else fail("expected DATABASES, TABLES, CATALOG or INDEXES after SHOW");
    return s;
}

// ---- DESCRIBE --------------------------------------------------------------
StmtPtr Parser::parseDescribe() {
    advance(); // DESCRIBE or DESC
    auto s = std::make_unique<DescribeStmt>();
    s->name = expectIdentifier("table name");
    return s;
}

// ---- INSERT ----------------------------------------------------------------
StmtPtr Parser::parseInsert() {
    expectKeyword("INSERT");
    expectKeyword("INTO");
    auto s = std::make_unique<InsertStmt>();
    s->table = expectIdentifier("table name");

    // Optional explicit column list.
    if (matchSymbol('(')) {
        s->columns.push_back(expectIdentifier("column name"));
        while (matchSymbol(',')) s->columns.push_back(expectIdentifier("column name"));
        expectSymbol(')');
    }

    expectKeyword("VALUES");
    expectSymbol('(');
    s->values.push_back(parseLiteral());
    while (matchSymbol(',')) s->values.push_back(parseLiteral());
    expectSymbol(')');
    return s;
}

// ---- SELECT ----------------------------------------------------------------
StmtPtr Parser::parseSelect() {
    expectKeyword("SELECT");
    auto s = std::make_unique<SelectStmt>();

    // Projection / aggregate list.
    if (matchSymbol('*')) {
        SelectItem it; it.star = true;
        s->items.push_back(it);
    } else {
        for (;;) {
            SelectItem it;
            const Token& t = peek();
            AggFunc fn = AggFunc::None;
            if (t.isKeyword("COUNT")) fn = AggFunc::Count;
            else if (t.isKeyword("SUM")) fn = AggFunc::Sum;
            else if (t.isKeyword("AVG")) fn = AggFunc::Avg;
            else if (t.isKeyword("MIN")) fn = AggFunc::Min;
            else if (t.isKeyword("MAX")) fn = AggFunc::Max;

            if (fn != AggFunc::None) {
                advance();
                it.agg = fn;
                expectSymbol('(');
                if (matchSymbol('*')) it.aggStar = true;
                else it.column = expectIdentifier("column name in aggregate");
                expectSymbol(')');
                s->isAggregate = true;
            } else {
                it.column = expectIdentifier("column name");
            }
            s->items.push_back(it);
            if (!matchSymbol(',')) break;
        }
    }

    expectKeyword("FROM");
    s->table = expectIdentifier("table name");

    s->where = parseWhere();

    if (matchKeyword("ORDER")) {
        expectKeyword("BY");
        s->orderBy = expectIdentifier("column name in ORDER BY");
        if (matchKeyword("DESC")) s->orderDesc = true;
        else if (matchKeyword("ASC")) s->orderDesc = false;
    }

    if (matchKeyword("LIMIT")) {
        if (peek().type != TokenType::IntLiteral) fail("expected an integer after LIMIT");
        s->hasLimit = true;
        s->limit = std::stoll(advance().text);
    }
    return s;
}

// ---- UPDATE ----------------------------------------------------------------
StmtPtr Parser::parseUpdate() {
    expectKeyword("UPDATE");
    auto s = std::make_unique<UpdateStmt>();
    s->table = expectIdentifier("table name");
    expectKeyword("SET");
    for (;;) {
        Assignment a;
        a.column = expectIdentifier("column name in SET");
        if (peek().type != TokenType::Operator || peek().text != "=")
            fail("expected '=' in SET clause");
        advance();
        a.value = parseLiteral();
        s->assignments.push_back(a);
        if (!matchSymbol(',')) break;
    }
    s->where = parseWhere();
    return s;
}

// ---- DELETE ----------------------------------------------------------------
StmtPtr Parser::parseDelete() {
    expectKeyword("DELETE");
    expectKeyword("FROM");
    auto s = std::make_unique<DeleteStmt>();
    s->table = expectIdentifier("table name");
    s->where = parseWhere();
    return s;
}

// ---- EXPLAIN ---------------------------------------------------------------
StmtPtr Parser::parseExplain() {
    expectKeyword("EXPLAIN");
    auto s = std::make_unique<ExplainStmt>();
    s->inner = parseStatement();  // explain whatever follows
    return s;
}

// ---- TRACE -----------------------------------------------------------------
StmtPtr Parser::parseTrace() {
    expectKeyword("TRACE");
    auto s = std::make_unique<TraceStmt>();
    if (matchKeyword("ON")) s->on = true;
    else if (matchKeyword("OFF")) s->on = false;
    else fail("expected ON or OFF after TRACE");
    return s;
}

// ---- EXEC ------------------------------------------------------------------
StmtPtr Parser::parseExec() {
    expectKeyword("EXEC");
    auto s = std::make_unique<ExecStmt>();
    if (peek().type != TokenType::StringLiteral)
        fail("expected a quoted file path after EXEC, e.g. EXEC 'demo.sql'");
    s->filename = advance().text;
    return s;
}

} // namespace minidb

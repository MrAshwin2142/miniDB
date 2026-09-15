// ast.h
// -----------------------------------------------------------------------------
// The parser produces one of these Statement objects. Each maps closely to a
// supported SQL-like command. The executor switches on Statement::kind and then
// static_casts down to the concrete type - a simple, allocation-light approach
// that keeps the whole grammar visible in one file.
// -----------------------------------------------------------------------------
#pragma once

#include "minidb/common.h"

#include <memory>
#include <string>
#include <vector>

namespace minidb {

enum class StmtKind {
    CreateDatabase, DropDatabase, UseDatabase, Show, CreateTable, DropTable,
    Describe, Insert, Select, Update, Delete, CreateIndex, DropIndex,
    Explain, Stats, Help, Trace, Exec, Exit
};

// Base class - just carries the kind tag and a virtual destructor.
struct Statement {
    explicit Statement(StmtKind k) : kind(k) {}
    virtual ~Statement() = default;
    StmtKind kind;
};

using StmtPtr = std::unique_ptr<Statement>;

// ---- A WHERE predicate: column <op> literal ---------------------------------
struct Condition {
    std::string column;
    std::string op;     // one of = != < <= > >=
    Value       value;  // right-hand literal
};

// ---- SELECT list item -------------------------------------------------------
enum class AggFunc { None, Count, Sum, Avg, Min, Max };

struct SelectItem {
    bool        star = false;        // "*"
    std::string column;              // column name (when not an aggregate)
    AggFunc     agg = AggFunc::None; // aggregate function, if any
    bool        aggStar = false;     // COUNT(*)
};

// ---- Concrete statements ----------------------------------------------------
struct CreateDatabaseStmt : Statement {
    CreateDatabaseStmt() : Statement(StmtKind::CreateDatabase) {}
    std::string name;
};

struct DropDatabaseStmt : Statement {
    DropDatabaseStmt() : Statement(StmtKind::DropDatabase) {}
    std::string name;
};

struct UseDatabaseStmt : Statement {
    UseDatabaseStmt() : Statement(StmtKind::UseDatabase) {}
    std::string name;
};

enum class ShowKind { Databases, Tables, Catalog, Indexes };
struct ShowStmt : Statement {
    ShowStmt() : Statement(StmtKind::Show) {}
    ShowKind what = ShowKind::Databases;
};

struct CreateTableStmt : Statement {
    CreateTableStmt() : Statement(StmtKind::CreateTable) {}
    std::string         name;
    std::vector<Column> columns;
};

struct DropTableStmt : Statement {
    DropTableStmt() : Statement(StmtKind::DropTable) {}
    std::string name;
};

struct DescribeStmt : Statement {
    DescribeStmt() : Statement(StmtKind::Describe) {}
    std::string name;
};

struct InsertStmt : Statement {
    InsertStmt() : Statement(StmtKind::Insert) {}
    std::string              table;
    std::vector<std::string> columns; // empty => all columns in schema order
    std::vector<Value>       values;  // raw literals, one per named/all column
};

struct SelectStmt : Statement {
    SelectStmt() : Statement(StmtKind::Select) {}
    std::string             table;
    std::vector<SelectItem> items;         // projection or aggregate list
    bool                    isAggregate = false;
    std::vector<Condition>  where;         // ANDed together
    std::string             orderBy;       // empty => no ORDER BY
    bool                    orderDesc = false;
    bool                    hasLimit = false;
    long long               limit = -1;
};

struct Assignment { std::string column; Value value; };

struct UpdateStmt : Statement {
    UpdateStmt() : Statement(StmtKind::Update) {}
    std::string             table;
    std::vector<Assignment> assignments;
    std::vector<Condition>  where;
};

struct DeleteStmt : Statement {
    DeleteStmt() : Statement(StmtKind::Delete) {}
    std::string            table;
    std::vector<Condition> where;
};

struct CreateIndexStmt : Statement {
    CreateIndexStmt() : Statement(StmtKind::CreateIndex) {}
    std::string indexName;
    std::string table;
    std::string column;
};

struct DropIndexStmt : Statement {
    DropIndexStmt() : Statement(StmtKind::DropIndex) {}
    std::string indexName;
};

struct ExplainStmt : Statement {
    ExplainStmt() : Statement(StmtKind::Explain) {}
    StmtPtr inner;   // the statement being explained
};

struct StatsStmt : Statement { StatsStmt() : Statement(StmtKind::Stats) {} };
struct HelpStmt  : Statement { HelpStmt()  : Statement(StmtKind::Help)  {} };

struct TraceStmt : Statement {
    TraceStmt() : Statement(StmtKind::Trace) {}
    bool on = true;
};

struct ExecStmt : Statement {
    ExecStmt() : Statement(StmtKind::Exec) {}
    std::string filename;
};

struct ExitStmt : Statement { ExitStmt() : Statement(StmtKind::Exit) {} };

} // namespace minidb

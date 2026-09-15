// executor.h
// -----------------------------------------------------------------------------
// The executor is the engine's brain. Given a parsed Statement it validates
// names and types against the catalog, chooses an access path (full table scan
// vs. B+ tree index seek), performs the read/write against the storage layer,
// and returns a Result the CLI knows how to print.
// -----------------------------------------------------------------------------
#pragma once

#include "minidb/ast.h"
#include "minidb/database.h"

#include <string>
#include <vector>

namespace minidb {

// Everything the CLI needs to render the outcome of one statement.
struct Result {
    bool isResultSet = false;                         // has columns + rows to print
    std::vector<std::string>              columns;
    std::vector<std::vector<std::string>> rows;

    std::string message;      // e.g. "3 rows inserted."
    std::string plan;         // EXPLAIN / trace plan text (may be multi-line)
    double      elapsedMs = 0.0;

    std::string execFile;     // set by EXEC => CLI should run this script file
    bool        quit = false; // set by EXIT/QUIT
};

class Executor {
public:
    explicit Executor(Engine& engine) : engine_(engine) {}

    Result execute(Statement& stmt);

    bool traceEnabled() const { return trace_; }

private:
    Engine& engine_;
    bool    trace_ = false;

    Database& requireDb();

    // Per-statement handlers.
    Result execCreateDatabase(CreateDatabaseStmt& s);
    Result execDropDatabase(DropDatabaseStmt& s);
    Result execUseDatabase(UseDatabaseStmt& s);
    Result execShow(ShowStmt& s);
    Result execCreateTable(CreateTableStmt& s);
    Result execDropTable(DropTableStmt& s);
    Result execDescribe(DescribeStmt& s);
    Result execInsert(InsertStmt& s);
    Result execSelect(SelectStmt& s);
    Result execUpdate(UpdateStmt& s);
    Result execDelete(DeleteStmt& s);
    Result execCreateIndex(CreateIndexStmt& s);
    Result execDropIndex(DropIndexStmt& s);
    Result execExplain(ExplainStmt& s);
    Result execStats(StatsStmt& s);
    Result execTrace(TraceStmt& s);

    // Shared helpers.
    Value coerce(const Value& literal, const Column& col) const;
    bool  passesAll(const Row& row, const Schema& schema,
                    const std::vector<Condition>& conds) const;
    bool  compareOp(int cmp, const std::string& op) const;

    // Plan chosen for a SELECT/UPDATE/DELETE with a WHERE clause.
    struct AccessPlan {
        bool        useIndex = false;
        std::string indexName;
        std::string indexColumn;
        int         equalityCondPos = -1; // which WHERE condition drives the seek
    };
    AccessPlan choosePlan(Database& db, const std::string& table,
                          const std::vector<Condition>& where);

    // Collect matching (RID,row) pairs using the chosen plan.
    std::vector<std::pair<RID, Row>>
    gatherRows(Database& db, const std::string& table,
               const std::vector<Condition>& where, const AccessPlan& plan,
               uint64_t& examined);
};

} // namespace minidb

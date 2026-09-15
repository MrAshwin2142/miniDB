// executor.cpp
#include "minidb/executor.h"
#include "minidb/utils.h"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace minidb {

// ---------------------------------------------------------------------------
// Small local helpers
// ---------------------------------------------------------------------------
namespace {

std::string typeLabel(const Column& c) {
    if (c.type == ColumnType::CHAR) return "CHAR(" + std::to_string(c.charLen) + ")";
    return columnTypeName(c.type);
}

double numericAsDouble(const Value& v) {
    return v.type == ColumnType::INT ? static_cast<double>(v.i) : v.d;
}

std::string aggHeader(const SelectItem& it) {
    const char* fn = "";
    switch (it.agg) {
        case AggFunc::Count: fn = "COUNT"; break;
        case AggFunc::Sum:   fn = "SUM";   break;
        case AggFunc::Avg:   fn = "AVG";   break;
        case AggFunc::Min:   fn = "MIN";   break;
        case AggFunc::Max:   fn = "MAX";   break;
        default:             fn = "?";     break;
    }
    return std::string(fn) + "(" + (it.aggStar ? "*" : it.column) + ")";
}

} // namespace

// ---------------------------------------------------------------------------
Database& Executor::requireDb() {
    Database* db = engine_.current();
    if (!db) throw DBError("no database selected. Run: USE <database>;");
    return *db;
}

bool Executor::compareOp(int cmp, const std::string& op) const {
    if (op == "=")  return cmp == 0;
    if (op == "!=") return cmp != 0;
    if (op == "<")  return cmp < 0;
    if (op == "<=") return cmp <= 0;
    if (op == ">")  return cmp > 0;
    if (op == ">=") return cmp >= 0;
    throw DBError("unknown operator: " + op);
}

Value Executor::coerce(const Value& lit, const Column& col) const {
    if (lit.isNull) return Value::makeNull(col.type);

    switch (col.type) {
        case ColumnType::INT:
            if (lit.type != ColumnType::INT)
                throw DBError("type mismatch for column '" + col.name + "': expected INT");
            return Value::makeInt(lit.i);
        case ColumnType::FLOAT:
            if (lit.type == ColumnType::INT)   return Value::makeFloat(static_cast<double>(lit.i));
            if (lit.type == ColumnType::FLOAT) return Value::makeFloat(lit.d);
            throw DBError("type mismatch for column '" + col.name + "': expected FLOAT");
        case ColumnType::TEXT: {
            if (lit.type != ColumnType::TEXT && lit.type != ColumnType::CHAR)
                throw DBError("type mismatch for column '" + col.name + "': expected TEXT");
            return Value::makeText(lit.s);
        }
        case ColumnType::CHAR: {
            if (lit.type != ColumnType::TEXT && lit.type != ColumnType::CHAR)
                throw DBError("type mismatch for column '" + col.name + "': expected CHAR");
            if (static_cast<int>(lit.s.size()) > col.charLen)
                throw DBError("value too long for CHAR(" + std::to_string(col.charLen) +
                              ") column '" + col.name + "'");
            Value v; v.type = ColumnType::CHAR; v.s = lit.s; return v;
        }
    }
    throw DBError("internal: unknown column type");
}

bool Executor::passesAll(const Row& row, const Schema& schema,
                         const std::vector<Condition>& conds) const {
    for (const auto& c : conds) {
        int idx = schema.columnIndex(c.column);
        if (idx < 0) throw DBError("no such column in WHERE: " + c.column);
        const Value& lhs = row[static_cast<std::size_t>(idx)];
        // SQL-like: any comparison against NULL yields "unknown" => not selected.
        if (lhs.isNull || c.value.isNull) return false;
        Value rhs = coerce(c.value, schema.columns[static_cast<std::size_t>(idx)]);
        int cmp = compareValues(lhs, rhs);
        if (!compareOp(cmp, c.op)) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Access path selection
// ---------------------------------------------------------------------------
Executor::AccessPlan Executor::choosePlan(Database& db, const std::string& table,
                                          const std::vector<Condition>& where) {
    AccessPlan plan;
    for (std::size_t k = 0; k < where.size(); ++k) {
        if (where[k].op != "=") continue;
        const IndexMeta* idx = db.catalog().indexOn(table, where[k].column);
        if (idx) {
            plan.useIndex = true;
            plan.indexName = idx->name;
            plan.indexColumn = idx->column;
            plan.equalityCondPos = static_cast<int>(k);
            break;
        }
    }
    return plan;
}

std::vector<std::pair<RID, Row>>
Executor::gatherRows(Database& db, const std::string& table,
                     const std::vector<Condition>& where, const AccessPlan& plan,
                     uint64_t& examined) {
    std::vector<std::pair<RID, Row>> out;
    examined = 0;
    const TableMeta* tmeta = db.catalog().getTable(table);
    const Schema& schema = tmeta->schema;
    Table& tbl = db.table(table);

    if (plan.useIndex) {
        const Condition& c = where[static_cast<std::size_t>(plan.equalityCondPos)];
        int colIdx = schema.columnIndex(c.column);
        Value key = coerce(c.value, schema.columns[static_cast<std::size_t>(colIdx)]);
        BPlusTree& tree = db.index(plan.indexName);
        std::vector<RID> rids = tree.findEqual(key);
        examined = rids.size();
        for (const RID& rid : rids) {
            Row row;
            if (!tbl.getRow(rid, row)) continue;
            if (passesAll(row, schema, where)) out.emplace_back(rid, row);
        }
    } else {
        tbl.scanAll([&](const RID& rid, const Row& row) {
            ++examined;
            if (passesAll(row, schema, where)) out.emplace_back(rid, row);
        });
    }
    return out;
}

// ---------------------------------------------------------------------------
// Dispatch (with timing)
// ---------------------------------------------------------------------------
Result Executor::execute(Statement& stmt) {
    auto t0 = std::chrono::steady_clock::now();
    Result r;

    switch (stmt.kind) {
        case StmtKind::CreateDatabase: r = execCreateDatabase(static_cast<CreateDatabaseStmt&>(stmt)); break;
        case StmtKind::DropDatabase:   r = execDropDatabase(static_cast<DropDatabaseStmt&>(stmt)); break;
        case StmtKind::UseDatabase:    r = execUseDatabase(static_cast<UseDatabaseStmt&>(stmt)); break;
        case StmtKind::Show:           r = execShow(static_cast<ShowStmt&>(stmt)); break;
        case StmtKind::CreateTable:    r = execCreateTable(static_cast<CreateTableStmt&>(stmt)); break;
        case StmtKind::DropTable:      r = execDropTable(static_cast<DropTableStmt&>(stmt)); break;
        case StmtKind::Describe:       r = execDescribe(static_cast<DescribeStmt&>(stmt)); break;
        case StmtKind::Insert:         r = execInsert(static_cast<InsertStmt&>(stmt)); break;
        case StmtKind::Select:         r = execSelect(static_cast<SelectStmt&>(stmt)); break;
        case StmtKind::Update:         r = execUpdate(static_cast<UpdateStmt&>(stmt)); break;
        case StmtKind::Delete:         r = execDelete(static_cast<DeleteStmt&>(stmt)); break;
        case StmtKind::CreateIndex:    r = execCreateIndex(static_cast<CreateIndexStmt&>(stmt)); break;
        case StmtKind::DropIndex:      r = execDropIndex(static_cast<DropIndexStmt&>(stmt)); break;
        case StmtKind::Explain:        r = execExplain(static_cast<ExplainStmt&>(stmt)); break;
        case StmtKind::Stats:          r = execStats(static_cast<StatsStmt&>(stmt)); break;
        case StmtKind::Trace:          r = execTrace(static_cast<TraceStmt&>(stmt)); break;
        case StmtKind::Help:
            r.message =
                "miniDB commands:\n"
                "  Databases : CREATE DATABASE n; DROP DATABASE n; USE n; SHOW DATABASES;\n"
                "  Tables    : CREATE TABLE t (col TYPE [PRIMARY KEY], ...); DROP TABLE t;\n"
                "              SHOW TABLES; DESCRIBE t;\n"
                "  Data      : INSERT INTO t [(cols)] VALUES (...);\n"
                "              SELECT col|*|COUNT(*)|SUM(c)|AVG(c) FROM t [WHERE c OP v [AND ...]]\n"
                "                     [ORDER BY c [ASC|DESC]] [LIMIT n];\n"
                "              UPDATE t SET c=v[,...] [WHERE ...]; DELETE FROM t [WHERE ...];\n"
                "  Indexes   : CREATE INDEX i ON t(col); DROP INDEX i; SHOW INDEXES;\n"
                "  Engine    : EXPLAIN <query>; SHOW CATALOG; STATS; TRACE ON|OFF;\n"
                "              EXEC 'script.sql'; HELP; EXIT;\n"
                "  Types     : INT, FLOAT, TEXT, CHAR(n).  Operators: = != < <= > >=";
            break;
        case StmtKind::Exec:
            r.execFile = static_cast<ExecStmt&>(stmt).filename;
            break;
        case StmtKind::Exit:
            r.quit = true;
            r.message = "Bye.";
            break;
    }

    auto t1 = std::chrono::steady_clock::now();
    r.elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return r;
}

// ---------------------------------------------------------------------------
// Database-level statements
// ---------------------------------------------------------------------------
Result Executor::execCreateDatabase(CreateDatabaseStmt& s) {
    engine_.createDatabase(s.name);
    Result r; r.message = "Database '" + s.name + "' created."; return r;
}

Result Executor::execDropDatabase(DropDatabaseStmt& s) {
    engine_.dropDatabase(s.name);
    Result r; r.message = "Database '" + s.name + "' dropped."; return r;
}

Result Executor::execUseDatabase(UseDatabaseStmt& s) {
    engine_.useDatabase(s.name);
    Result r; r.message = "Using database '" + s.name + "'."; return r;
}

Result Executor::execShow(ShowStmt& s) {
    Result r;
    if (s.what == ShowKind::Databases) {
        r.isResultSet = true;
        r.columns = {"Database"};
        for (const auto& name : engine_.listDatabases()) r.rows.push_back({name});
        return r;
    }

    Database& db = requireDb();
    if (s.what == ShowKind::Tables) {
        r.isResultSet = true;
        r.columns = {"Table"};
        for (const auto& t : db.catalog().tables()) r.rows.push_back({t.name});
    } else if (s.what == ShowKind::Indexes) {
        r.isResultSet = true;
        r.columns = {"Index", "Table", "Column"};
        for (const auto& i : db.catalog().indexes())
            r.rows.push_back({i.name, i.table, i.column});
    } else { // Catalog dump
        std::ostringstream os;
        os << "Catalog for database '" << db.name() << "':\n";
        for (const auto& t : db.catalog().tables()) {
            os << "  TABLE " << t.name << " (";
            for (std::size_t k = 0; k < t.schema.columns.size(); ++k) {
                const Column& c = t.schema.columns[k];
                os << (k ? ", " : "") << c.name << " " << typeLabel(c);
                if (c.primaryKey) os << " PK";
            }
            os << ")\n";
        }
        auto idxs = db.catalog().indexes();
        if (idxs.empty()) os << "  (no indexes)\n";
        for (const auto& i : idxs)
            os << "  INDEX " << i.name << " ON " << i.table << "(" << i.column << ")\n";
        r.message = os.str();
    }
    return r;
}

// ---------------------------------------------------------------------------
// DDL
// ---------------------------------------------------------------------------
Result Executor::execCreateTable(CreateTableStmt& s) {
    Database& db = requireDb();

    Schema schema;
    schema.primaryKeyIndex = -1;
    for (const auto& col : s.columns) {
        // Reject duplicate column names.
        if (schema.columnIndex(col.name) >= 0)
            throw DBError("duplicate column name: " + col.name);
        if (col.primaryKey) {
            if (schema.primaryKeyIndex >= 0)
                throw DBError("only one PRIMARY KEY is allowed per table");
            schema.primaryKeyIndex = static_cast<int>(schema.columns.size());
        }
        schema.columns.push_back(col);
    }
    if (schema.columns.empty())
        throw DBError("a table needs at least one column");

    db.createTable(s.name, schema);
    Result r; r.message = "Table '" + s.name + "' created with " +
                          std::to_string(schema.columns.size()) + " column(s).";
    return r;
}

Result Executor::execDropTable(DropTableStmt& s) {
    Database& db = requireDb();
    db.dropTable(s.name);
    Result r; r.message = "Table '" + s.name + "' dropped."; return r;
}

Result Executor::execDescribe(DescribeStmt& s) {
    Database& db = requireDb();
    const TableMeta* t = db.catalog().getTable(s.name);
    if (!t) throw DBError("no such table: " + s.name);

    Result r;
    r.isResultSet = true;
    r.columns = {"Column", "Type", "PrimaryKey"};
    for (const auto& c : t->schema.columns)
        r.rows.push_back({c.name, typeLabel(c), c.primaryKey ? "YES" : "NO"});
    return r;
}

// ---------------------------------------------------------------------------
// INSERT
// ---------------------------------------------------------------------------
Result Executor::execInsert(InsertStmt& s) {
    Database& db = requireDb();
    const TableMeta* tmeta = db.catalog().getTable(s.table);
    if (!tmeta) throw DBError("no such table: " + s.table);
    const Schema& schema = tmeta->schema;
    std::size_t ncols = schema.columns.size();

    Row row(ncols);
    for (std::size_t k = 0; k < ncols; ++k) row[k] = Value::makeNull(schema.columns[k].type);

    if (s.columns.empty()) {
        if (s.values.size() != ncols)
            throw DBError("column count mismatch: table has " + std::to_string(ncols) +
                          " columns but " + std::to_string(s.values.size()) + " values given");
        for (std::size_t k = 0; k < ncols; ++k)
            row[k] = coerce(s.values[k], schema.columns[k]);
    } else {
        if (s.columns.size() != s.values.size())
            throw DBError("column/value count mismatch in INSERT");
        std::vector<bool> seen(ncols, false);
        for (std::size_t j = 0; j < s.columns.size(); ++j) {
            int idx = schema.columnIndex(s.columns[j]);
            if (idx < 0) throw DBError("no such column: " + s.columns[j]);
            if (seen[static_cast<std::size_t>(idx)])
                throw DBError("column specified twice: " + s.columns[j]);
            seen[static_cast<std::size_t>(idx)] = true;
            row[static_cast<std::size_t>(idx)] =
                coerce(s.values[j], schema.columns[static_cast<std::size_t>(idx)]);
        }
    }

    // Primary key: not null + unique.
    if (schema.primaryKeyIndex >= 0) {
        std::size_t pk = static_cast<std::size_t>(schema.primaryKeyIndex);
        if (row[pk].isNull)
            throw DBError("primary key column '" + schema.columns[pk].name + "' cannot be NULL");

        bool duplicate = false;
        const IndexMeta* pkIdx = db.catalog().indexOn(tmeta->name, schema.columns[pk].name);
        if (pkIdx) {
            duplicate = !db.index(pkIdx->name).findEqual(row[pk]).empty();
        } else {
            Table& tbl = db.table(s.table);
            tbl.scanAll([&](const RID&, const Row& existing) {
                if (!duplicate && compareValues(existing[pk], row[pk]) == 0) duplicate = true;
            });
        }
        if (duplicate)
            throw DBError("duplicate value for primary key '" + schema.columns[pk].name + "'");
    }

    Table& tbl = db.table(s.table);
    RID rid = tbl.insertRow(row);

    for (auto& oi : db.openIndexesForTable(tmeta->name))
        oi.tree->insert(row[static_cast<std::size_t>(oi.colIndex)], rid);
    db.flushIndexes();
    tbl.flush();

    Result r; r.message = "1 row inserted."; return r;
}

// ---------------------------------------------------------------------------
// SELECT
// ---------------------------------------------------------------------------
Result Executor::execSelect(SelectStmt& s) {
    Database& db = requireDb();
    const TableMeta* tmeta = db.catalog().getTable(s.table);
    if (!tmeta) throw DBError("no such table: " + s.table);
    const Schema& schema = tmeta->schema;

    AccessPlan plan = choosePlan(db, tmeta->name, s.where);
    uint64_t examined = 0;
    auto matches = gatherRows(db, tmeta->name, s.where, plan, examined);

    // ORDER BY (stable) - applies to projections; harmless for aggregates.
    if (!s.orderBy.empty()) {
        int oc = schema.columnIndex(s.orderBy);
        if (oc < 0) throw DBError("no such column in ORDER BY: " + s.orderBy);
        std::stable_sort(matches.begin(), matches.end(),
            [&](const std::pair<RID, Row>& a, const std::pair<RID, Row>& b) {
                int cmp = compareValues(a.second[static_cast<std::size_t>(oc)],
                                        b.second[static_cast<std::size_t>(oc)]);
                return s.orderDesc ? cmp > 0 : cmp < 0;
            });
    }

    Result r;

    if (s.isAggregate) {
        r.isResultSet = true;
        std::vector<std::string> outRow;
        for (const auto& it : s.items) {
            if (it.agg == AggFunc::None)
                throw DBError("cannot mix plain columns and aggregates in one SELECT");
            r.columns.push_back(aggHeader(it));

            int colIdx = -1;
            if (!it.aggStar) {
                colIdx = schema.columnIndex(it.column);
                if (colIdx < 0) throw DBError("no such column: " + it.column);
            }

            if (it.agg == AggFunc::Count) {
                long long cnt = 0;
                for (const auto& m : matches) {
                    if (it.aggStar || !m.second[static_cast<std::size_t>(colIdx)].isNull) ++cnt;
                }
                outRow.push_back(std::to_string(cnt));
            } else {
                // Numeric-only aggregates.
                const Column& col = schema.columns[static_cast<std::size_t>(colIdx)];
                bool numeric = (col.type == ColumnType::INT || col.type == ColumnType::FLOAT);
                if ((it.agg == AggFunc::Sum || it.agg == AggFunc::Avg) && !numeric)
                    throw DBError("SUM/AVG require a numeric column: " + it.column);

                bool any = false;
                double sum = 0.0; long long n = 0;
                Value best; bool haveBest = false;
                for (const auto& m : matches) {
                    const Value& v = m.second[static_cast<std::size_t>(colIdx)];
                    if (v.isNull) continue;
                    any = true; ++n;
                    if (numeric) sum += numericAsDouble(v);
                    if (!haveBest) { best = v; haveBest = true; }
                    else {
                        int cmp = compareValues(v, best);
                        if ((it.agg == AggFunc::Min && cmp < 0) ||
                            (it.agg == AggFunc::Max && cmp > 0)) best = v;
                    }
                }
                if (!any) { outRow.push_back("NULL"); continue; }

                if (it.agg == AggFunc::Sum) {
                    if (col.type == ColumnType::INT)
                        outRow.push_back(std::to_string(static_cast<long long>(sum)));
                    else outRow.push_back(Value::makeFloat(sum).toString());
                } else if (it.agg == AggFunc::Avg) {
                    outRow.push_back(Value::makeFloat(sum / static_cast<double>(n)).toString());
                } else { // Min / Max
                    outRow.push_back(best.toString());
                }
            }
        }
        r.rows.push_back(outRow);
        r.message = "1 row.";
    } else {
        // Projection.
        std::vector<int> outCols;
        if (s.items.size() == 1 && s.items[0].star) {
            for (std::size_t k = 0; k < schema.columns.size(); ++k) {
                outCols.push_back(static_cast<int>(k));
                r.columns.push_back(schema.columns[k].name);
            }
        } else {
            for (const auto& it : s.items) {
                if (it.star) throw DBError("'*' cannot be combined with other columns");
                int c = schema.columnIndex(it.column);
                if (c < 0) throw DBError("no such column: " + it.column);
                outCols.push_back(c);
                r.columns.push_back(schema.columns[static_cast<std::size_t>(c)].name);
            }
        }

        std::size_t limit = matches.size();
        if (s.hasLimit && s.limit >= 0 && static_cast<std::size_t>(s.limit) < limit)
            limit = static_cast<std::size_t>(s.limit);

        r.isResultSet = true;
        for (std::size_t i = 0; i < limit; ++i) {
            std::vector<std::string> outRow;
            for (int c : outCols)
                outRow.push_back(matches[i].second[static_cast<std::size_t>(c)].toString());
            r.rows.push_back(outRow);
        }
        r.message = std::to_string(r.rows.size()) + " row(s).";
    }

    if (trace_) {
        std::ostringstream os;
        os << "Access path : "
           << (plan.useIndex ? ("Index seek (" + plan.indexName + " on " + plan.indexColumn + ")")
                             : std::string("Full table scan"))
           << "\nRows examined: " << examined
           << "\nRows matched : " << matches.size();
        r.plan = os.str();
    }
    return r;
}

// ---------------------------------------------------------------------------
// UPDATE
// ---------------------------------------------------------------------------
Result Executor::execUpdate(UpdateStmt& s) {
    Database& db = requireDb();
    const TableMeta* tmeta = db.catalog().getTable(s.table);
    if (!tmeta) throw DBError("no such table: " + s.table);
    const Schema& schema = tmeta->schema;

    AccessPlan plan = choosePlan(db, tmeta->name, s.where);
    uint64_t examined = 0;
    auto matches = gatherRows(db, tmeta->name, s.where, plan, examined);

    Table& tbl = db.table(s.table);
    auto indexes = db.openIndexesForTable(tmeta->name);

    long long affected = 0;
    for (auto& m : matches) {
        RID rid = m.first;
        Row newRow = m.second;
        for (const auto& a : s.assignments) {
            int idx = schema.columnIndex(a.column);
            if (idx < 0) throw DBError("no such column: " + a.column);
            if (idx == schema.primaryKeyIndex && a.value.isNull)
                throw DBError("primary key cannot be set to NULL");
            newRow[static_cast<std::size_t>(idx)] =
                coerce(a.value, schema.columns[static_cast<std::size_t>(idx)]);
        }

        // Remove old index entries (key + old RID), then relocate/write.
        for (auto& oi : indexes)
            oi.tree->remove(m.second[static_cast<std::size_t>(oi.colIndex)], rid);
        RID newRid = tbl.updateRow(rid, newRow);
        for (auto& oi : indexes)
            oi.tree->insert(newRow[static_cast<std::size_t>(oi.colIndex)], newRid);
        ++affected;
    }
    db.flushIndexes();
    tbl.flush();

    Result r; r.message = std::to_string(affected) + " row(s) updated."; return r;
}

// ---------------------------------------------------------------------------
// DELETE
// ---------------------------------------------------------------------------
Result Executor::execDelete(DeleteStmt& s) {
    Database& db = requireDb();
    const TableMeta* tmeta = db.catalog().getTable(s.table);
    if (!tmeta) throw DBError("no such table: " + s.table);

    AccessPlan plan = choosePlan(db, tmeta->name, s.where);
    uint64_t examined = 0;
    auto matches = gatherRows(db, tmeta->name, s.where, plan, examined);

    Table& tbl = db.table(s.table);
    auto indexes = db.openIndexesForTable(tmeta->name);

    long long affected = 0;
    for (auto& m : matches) {
        for (auto& oi : indexes)
            oi.tree->remove(m.second[static_cast<std::size_t>(oi.colIndex)], m.first);
        tbl.deleteRow(m.first);
        ++affected;
    }
    db.flushIndexes();
    tbl.flush();

    Result r; r.message = std::to_string(affected) + " row(s) deleted."; return r;
}

// ---------------------------------------------------------------------------
// Index DDL
// ---------------------------------------------------------------------------
Result Executor::execCreateIndex(CreateIndexStmt& s) {
    Database& db = requireDb();
    db.createIndex(s.indexName, s.table, s.column);
    Result r; r.message = "Index '" + s.indexName + "' created on " +
                          s.table + "(" + s.column + ")."; return r;
}

Result Executor::execDropIndex(DropIndexStmt& s) {
    Database& db = requireDb();
    db.dropIndex(s.indexName);
    Result r; r.message = "Index '" + s.indexName + "' dropped."; return r;
}

// ---------------------------------------------------------------------------
// EXPLAIN
// ---------------------------------------------------------------------------
Result Executor::execExplain(ExplainStmt& s) {
    Database& db = requireDb();
    Result r;
    std::ostringstream os;
    os << "Query Plan\n----------\n";

    Statement& inner = *s.inner;
    if (inner.kind == StmtKind::Select || inner.kind == StmtKind::Update ||
        inner.kind == StmtKind::Delete) {

        std::string table;
        std::vector<Condition>* where = nullptr;
        const char* verb = "SELECT";
        if (inner.kind == StmtKind::Select) {
            auto& q = static_cast<SelectStmt&>(inner); table = q.table; where = &q.where; verb = "SELECT";
        } else if (inner.kind == StmtKind::Update) {
            auto& q = static_cast<UpdateStmt&>(inner); table = q.table; where = &q.where; verb = "UPDATE";
        } else {
            auto& q = static_cast<DeleteStmt&>(inner); table = q.table; where = &q.where; verb = "DELETE";
        }

        const TableMeta* tmeta = db.catalog().getTable(table);
        if (!tmeta) throw DBError("no such table: " + table);

        AccessPlan plan = choosePlan(db, tmeta->name, *where);
        uint64_t examined = 0;
        auto matches = gatherRows(db, tmeta->name, *where, plan, examined); // read-only

        os << "Statement    : " << verb << "\n"
           << "Table        : " << tmeta->name << "\n"
           << "Access path  : "
           << (plan.useIndex ? ("Index seek (" + plan.indexName + " on " + plan.indexColumn + ")")
                             : std::string("Full table scan"))
           << "\n"
           << "Index used   : " << (plan.useIndex ? plan.indexName : "(none)") << "\n"
           << "Rows examined: " << examined << "\n"
           << "Rows matched : " << matches.size() << "\n";
    } else {
        os << "Statement    : (DDL / utility)\n"
           << "Access path  : n/a\n";
    }

    r.plan = os.str();
    return r;
}

// ---------------------------------------------------------------------------
// STATS
// ---------------------------------------------------------------------------
Result Executor::execStats(StatsStmt&) {
    Result r;
    std::ostringstream os;
    os << "miniDB engine statistics\n"
       << "  data root       : " << engine_.dataRoot() << "\n"
       << "  databases       : " << engine_.listDatabases().size() << "\n";

    Database* db = engine_.current();
    if (!db) {
        os << "  current database: (none selected)\n";
        r.message = os.str();
        return r;
    }

    os << "  current database: " << db->name() << "\n"
       << "  tables          : " << db->catalog().tables().size() << "\n";
    for (const auto& t : db->catalog().tables()) {
        Table& tbl = db->table(t.name);
        os << "    - " << t.name << " : " << tbl.liveRowCount() << " row(s), "
           << tbl.pageCount() << " page(s)\n";
    }
    os << "  indexes         : " << db->catalog().indexes().size() << "\n";
    for (const auto& i : db->catalog().indexes()) {
        BPlusTree& tree = db->index(i.name);
        os << "    - " << i.name << " on " << i.table << "(" << i.column << ") : "
           << tree.size() << " entries, height " << tree.height() << "\n";
    }
    r.message = os.str();
    return r;
}

Result Executor::execTrace(TraceStmt& s) {
    trace_ = s.on;
    Result r; r.message = std::string("Trace ") + (s.on ? "enabled." : "disabled."); return r;
}

} // namespace minidb

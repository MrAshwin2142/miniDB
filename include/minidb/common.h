// common.h
// -----------------------------------------------------------------------------
// Core value / type / schema definitions shared across every layer of miniDB.
// Keeping these in one place means the lexer, parser, storage, index and
// executor all speak the same vocabulary for "what a column is" and "what a
// value is".
// -----------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

namespace minidb {

// Size of a single on-disk page. All heap and index files are managed as an
// array of fixed-size pages of exactly this many bytes.
constexpr std::size_t PAGE_SIZE = 4096;

// -----------------------------------------------------------------------------
// Column types
// -----------------------------------------------------------------------------
enum class ColumnType {
    INT,     // 64-bit signed integer
    FLOAT,   // 64-bit double
    TEXT,    // variable length string
    CHAR     // fixed length string, CHAR(n)
};

std::string columnTypeName(ColumnType t);

// A single column definition inside a table schema.
struct Column {
    std::string name;
    ColumnType  type = ColumnType::INT;
    int         charLen = 0;      // only meaningful for CHAR(n)
    bool        primaryKey = false;
};

// -----------------------------------------------------------------------------
// Value: a single typed cell. We use an explicit tagged struct rather than
// std::variant because it makes on-disk serialization and comparison logic
// very direct and easy to explain.
// -----------------------------------------------------------------------------
struct Value {
    ColumnType  type = ColumnType::INT;
    bool        isNull = false;
    int64_t     i = 0;      // INT
    double      d = 0.0;    // FLOAT
    std::string s;          // TEXT / CHAR

    static Value makeNull(ColumnType t) { Value v; v.type = t; v.isNull = true; return v; }
    static Value makeInt(int64_t x)     { Value v; v.type = ColumnType::INT;   v.i = x; return v; }
    static Value makeFloat(double x)    { Value v; v.type = ColumnType::FLOAT; v.d = x; return v; }
    static Value makeText(std::string x){ Value v; v.type = ColumnType::TEXT;  v.s = std::move(x); return v; }

    // Human-readable form used by the output formatter.
    std::string toString() const;
};

// Three-way comparison between two values. Returns <0, 0, or >0.
// INT and FLOAT are compared numerically (and are cross-comparable); TEXT and
// CHAR are compared lexicographically. NULLs sort before everything else.
int compareValues(const Value& a, const Value& b);

// -----------------------------------------------------------------------------
// RID (Record IDentifier): the physical address of a row inside a heap file.
// A row lives in slot `slotId` of page `pageId`. Index leaves store RIDs so a
// key lookup can jump straight to the row without scanning the table.
// -----------------------------------------------------------------------------
struct RID {
    uint32_t pageId = 0;
    uint16_t slotId = 0;

    bool operator==(const RID& o) const { return pageId == o.pageId && slotId == o.slotId; }
    std::string toString() const;
};

// -----------------------------------------------------------------------------
// A schema is an ordered list of columns plus a cached primary-key index.
// -----------------------------------------------------------------------------
struct Schema {
    std::vector<Column> columns;
    int primaryKeyIndex = -1;   // index into `columns`, or -1 if none

    int columnIndex(const std::string& name) const;   // -1 if not found
    const Column* column(const std::string& name) const;
};

// A row is just an ordered vector of values matching the schema column order.
using Row = std::vector<Value>;

// -----------------------------------------------------------------------------
// DBError: the single exception type the engine throws for anything the user
// did wrong (bad SQL, missing table, type mismatch, ...). The CLI catches it
// and prints a clean "Error: ..." line instead of crashing.
// -----------------------------------------------------------------------------
class DBError : public std::runtime_error {
public:
    explicit DBError(const std::string& msg) : std::runtime_error(msg) {}
};

} // namespace minidb

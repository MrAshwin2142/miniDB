// catalog.h
// -----------------------------------------------------------------------------
// The catalog is the database's data dictionary: it remembers every table's
// schema and every index definition, and persists them to a single
// `catalog.meta` text file so the engine can reload the whole database on
// startup. It stores *metadata only* - the actual rows live in the heap files.
// -----------------------------------------------------------------------------
#pragma once

#include "minidb/common.h"

#include <string>
#include <vector>

namespace minidb {

struct TableMeta {
    std::string name;
    Schema      schema;
};

struct IndexMeta {
    std::string name;    // index name (unique within the database)
    std::string table;   // table it indexes
    std::string column;  // indexed column
};

class Catalog {
public:
    // Load from `path` if it exists; otherwise start empty.
    void load(const std::string& path);
    // Persist the whole catalog to `path`.
    void save(const std::string& path) const;

    // --- tables ---
    bool             hasTable(const std::string& name) const;
    const TableMeta* getTable(const std::string& name) const;
    void             addTable(const TableMeta& t);
    void             dropTable(const std::string& name);
    const std::vector<TableMeta>& tables() const { return tables_; }

    // --- indexes ---
    bool             hasIndex(const std::string& name) const;
    const IndexMeta* getIndex(const std::string& name) const;
    void             addIndex(const IndexMeta& idx);
    void             dropIndex(const std::string& name);
    const std::vector<IndexMeta>& indexes() const { return indexes_; }

    // All indexes defined on a given table.
    std::vector<IndexMeta> indexesForTable(const std::string& table) const;
    // The index (if any) on a specific table+column, else nullptr.
    const IndexMeta* indexOn(const std::string& table, const std::string& column) const;

private:
    std::vector<TableMeta> tables_;
    std::vector<IndexMeta> indexes_;
};

} // namespace minidb

// catalog.cpp
#include "minidb/catalog.h"
#include "minidb/utils.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace minidb {

// ---------------------------------------------------------------------------
// Persistence. The on-disk format is deliberately simple and human-readable so
// you can `cat` the catalog file during a demo and explain exactly what it
// holds:
//
//   TABLE users 3 0
//   COL id 0 0 1
//   COL name 2 0 0
//   COL age 0 0 0
//   INDEX idx_age users age
// ---------------------------------------------------------------------------
void Catalog::load(const std::string& path) {
    tables_.clear();
    indexes_.clear();
    if (!fs::exists(path)) return;

    std::ifstream in(path);
    if (!in) throw DBError("cannot read catalog: " + path);

    std::string line;
    TableMeta* current = nullptr;
    int remainingCols = 0;

    while (std::getline(in, line)) {
        line = util::trim(line);
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string kind;
        ss >> kind;

        if (kind == "TABLE") {
            TableMeta t;
            int ncols = 0;
            ss >> t.name >> ncols >> t.schema.primaryKeyIndex;
            tables_.push_back(t);
            current = &tables_.back();
            remainingCols = ncols;
        } else if (kind == "COL") {
            if (!current || remainingCols <= 0)
                throw DBError("corrupt catalog: unexpected COL line");
            Column c;
            int typeInt = 0, pk = 0;
            ss >> c.name >> typeInt >> c.charLen >> pk;
            c.type = static_cast<ColumnType>(typeInt);
            c.primaryKey = (pk != 0);
            current->schema.columns.push_back(c);
            --remainingCols;
        } else if (kind == "INDEX") {
            IndexMeta idx;
            ss >> idx.name >> idx.table >> idx.column;
            indexes_.push_back(idx);
        }
    }
}

void Catalog::save(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out) throw DBError("cannot write catalog: " + path);

    for (const auto& t : tables_) {
        out << "TABLE " << t.name << ' ' << t.schema.columns.size() << ' '
            << t.schema.primaryKeyIndex << '\n';
        for (const auto& c : t.schema.columns) {
            out << "COL " << c.name << ' ' << static_cast<int>(c.type) << ' '
                << c.charLen << ' ' << (c.primaryKey ? 1 : 0) << '\n';
        }
    }
    for (const auto& idx : indexes_) {
        out << "INDEX " << idx.name << ' ' << idx.table << ' ' << idx.column << '\n';
    }
}

// ---- tables ----------------------------------------------------------------
bool Catalog::hasTable(const std::string& name) const {
    return getTable(name) != nullptr;
}

const TableMeta* Catalog::getTable(const std::string& name) const {
    for (const auto& t : tables_)
        if (util::iequals(t.name, name)) return &t;
    return nullptr;
}

void Catalog::addTable(const TableMeta& t) {
    tables_.push_back(t);
}

void Catalog::dropTable(const std::string& name) {
    for (auto it = tables_.begin(); it != tables_.end(); ++it) {
        if (util::iequals(it->name, name)) { tables_.erase(it); break; }
    }
    // Also remove any indexes that referenced this table.
    for (auto it = indexes_.begin(); it != indexes_.end();) {
        if (util::iequals(it->table, name)) it = indexes_.erase(it);
        else ++it;
    }
}

// ---- indexes ---------------------------------------------------------------
bool Catalog::hasIndex(const std::string& name) const {
    return getIndex(name) != nullptr;
}

const IndexMeta* Catalog::getIndex(const std::string& name) const {
    for (const auto& i : indexes_)
        if (util::iequals(i.name, name)) return &i;
    return nullptr;
}

void Catalog::addIndex(const IndexMeta& idx) {
    indexes_.push_back(idx);
}

void Catalog::dropIndex(const std::string& name) {
    for (auto it = indexes_.begin(); it != indexes_.end(); ++it) {
        if (util::iequals(it->name, name)) { indexes_.erase(it); break; }
    }
}

std::vector<IndexMeta> Catalog::indexesForTable(const std::string& table) const {
    std::vector<IndexMeta> out;
    for (const auto& i : indexes_)
        if (util::iequals(i.table, table)) out.push_back(i);
    return out;
}

const IndexMeta* Catalog::indexOn(const std::string& table, const std::string& column) const {
    for (const auto& i : indexes_)
        if (util::iequals(i.table, table) && util::iequals(i.column, column)) return &i;
    return nullptr;
}

} // namespace minidb

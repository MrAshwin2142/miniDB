// database.cpp
#include "minidb/database.h"
#include "minidb/utils.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace minidb {

// ===========================================================================
// Database
// ===========================================================================
Database::Database(std::string dirPath, std::string name)
    : dirPath_(std::move(dirPath)), name_(std::move(name)) {
    catalog_.load(catalogPath());
}

std::string Database::tablePath(const std::string& t) const {
    return dirPath_ + "/" + t + ".tbl";
}
std::string Database::indexPath(const std::string& i) const {
    return dirPath_ + "/" + i + ".idx";
}
std::string Database::catalogPath() const {
    return dirPath_ + "/catalog.meta";
}

void Database::saveCatalog() const {
    catalog_.save(catalogPath());
}

// ---- Tables ----------------------------------------------------------------
Table& Database::table(const std::string& name) {
    const TableMeta* meta = catalog_.getTable(name);
    if (!meta) throw DBError("no such table: " + name);

    auto it = tableCache_.find(meta->name);
    if (it != tableCache_.end()) return *it->second;

    auto tbl = std::make_unique<Table>(tablePath(meta->name), meta->schema);
    Table& ref = *tbl;
    tableCache_[meta->name] = std::move(tbl);
    return ref;
}

void Database::createTable(const std::string& name, const Schema& schema) {
    if (catalog_.hasTable(name))
        throw DBError("table already exists: " + name);

    TableMeta meta;
    meta.name = name;
    meta.schema = schema;
    catalog_.addTable(meta);
    saveCatalog();

    // Touch the heap file so it exists immediately.
    (void)table(name);
}

void Database::dropTable(const std::string& name) {
    const TableMeta* meta = catalog_.getTable(name);
    if (!meta) throw DBError("no such table: " + name);
    std::string canonical = meta->name;

    // Close and delete any indexes on this table first (release file handles).
    for (const auto& idx : catalog_.indexesForTable(canonical)) {
        indexCache_.erase(idx.name);
        std::error_code ec;
        fs::remove(indexPath(idx.name), ec);
    }

    // Close and delete the heap file.
    tableCache_.erase(canonical);
    std::error_code ec;
    fs::remove(tablePath(canonical), ec);

    catalog_.dropTable(canonical);
    saveCatalog();
}

// ---- Indexes ---------------------------------------------------------------
BPlusTree& Database::index(const std::string& indexName) {
    const IndexMeta* meta = catalog_.getIndex(indexName);
    if (!meta) throw DBError("no such index: " + indexName);

    auto it = indexCache_.find(meta->name);
    if (it != indexCache_.end()) return *it->second;

    const TableMeta* tmeta = catalog_.getTable(meta->table);
    if (!tmeta) throw DBError("index references missing table: " + meta->table);
    int col = tmeta->schema.columnIndex(meta->column);
    if (col < 0) throw DBError("index references missing column: " + meta->column);

    auto tree = std::make_unique<BPlusTree>(tmeta->schema.columns[col].type);
    tree->loadFromFile(indexPath(meta->name));
    BPlusTree& ref = *tree;
    indexCache_[meta->name] = std::move(tree);
    return ref;
}

void Database::createIndex(const std::string& indexName,
                           const std::string& tableName,
                           const std::string& column) {
    if (catalog_.hasIndex(indexName))
        throw DBError("index already exists: " + indexName);
    const TableMeta* tmeta = catalog_.getTable(tableName);
    if (!tmeta) throw DBError("no such table: " + tableName);
    int col = tmeta->schema.columnIndex(column);
    if (col < 0) throw DBError("no such column: " + column + " in table " + tableName);

    // Register in the catalog first so index()/table() can resolve names.
    IndexMeta meta;
    meta.name = indexName;
    meta.table = tmeta->name;
    meta.column = tmeta->schema.columns[col].name;
    catalog_.addIndex(meta);
    saveCatalog();

    // Build the tree by scanning existing rows.
    auto tree = std::make_unique<BPlusTree>(tmeta->schema.columns[col].type);
    Table& tbl = table(tmeta->name);
    tbl.scanAll([&](const RID& rid, const Row& row) {
        tree->insert(row[static_cast<std::size_t>(col)], rid);
    });
    tree->saveToFile(indexPath(indexName));
    indexCache_[indexName] = std::move(tree);
}

void Database::dropIndex(const std::string& indexName) {
    const IndexMeta* meta = catalog_.getIndex(indexName);
    if (!meta) throw DBError("no such index: " + indexName);
    std::string canonical = meta->name;

    indexCache_.erase(canonical);
    std::error_code ec;
    fs::remove(indexPath(canonical), ec);

    catalog_.dropIndex(canonical);
    saveCatalog();
}

void Database::flushIndexes() {
    for (auto& kv : indexCache_)
        kv.second->saveToFile(indexPath(kv.first));
}

std::vector<OpenIndex> Database::openIndexesForTable(const std::string& tableName) {
    std::vector<OpenIndex> out;
    const TableMeta* tmeta = catalog_.getTable(tableName);
    if (!tmeta) return out;
    for (const auto& idx : catalog_.indexesForTable(tmeta->name)) {
        OpenIndex oi;
        oi.name = idx.name;
        oi.colIndex = tmeta->schema.columnIndex(idx.column);
        oi.tree = &index(idx.name);
        out.push_back(oi);
    }
    return out;
}

// ===========================================================================
// Engine
// ===========================================================================
Engine::Engine(std::string dataRoot) : dataRoot_(std::move(dataRoot)) {
    std::error_code ec;
    fs::create_directories(dataRoot_, ec);
}

std::string Engine::dbPath(const std::string& name) const {
    return dataRoot_ + "/" + name;
}

void Engine::createDatabase(const std::string& name) {
    std::string path = dbPath(name);
    if (fs::exists(path))
        throw DBError("database already exists: " + name);
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec) throw DBError("cannot create database directory: " + name);
    // Write an empty catalog so the directory is a valid database immediately.
    Catalog empty;
    empty.save(path + "/catalog.meta");
}

void Engine::dropDatabase(const std::string& name) {
    std::string path = dbPath(name);
    if (!fs::exists(path))
        throw DBError("no such database: " + name);
    // If it is the current database, close it first to release file handles.
    if (util::iequals(name, currentName_)) {
        current_.reset();
        currentName_.clear();
    }
    std::error_code ec;
    fs::remove_all(path, ec);
    if (ec) throw DBError("cannot drop database: " + name);
}

void Engine::useDatabase(const std::string& name) {
    std::string path = dbPath(name);
    if (!fs::exists(path))
        throw DBError("no such database: " + name);
    current_ = std::make_unique<Database>(path, name);
    currentName_ = name;
}

std::vector<std::string> Engine::listDatabases() const {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::exists(dataRoot_)) return out;
    for (const auto& entry : fs::directory_iterator(dataRoot_, ec)) {
        if (entry.is_directory()) out.push_back(entry.path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace minidb

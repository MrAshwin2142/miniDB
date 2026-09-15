// database.h
// -----------------------------------------------------------------------------
// Two coordinating classes:
//
//   Database - one open database directory. Owns its Catalog and lazily opens
//              the heap files (Table) and B+ tree indexes it needs, caching the
//              handles. This is where "a table" and "an index" become live
//              objects backed by files.
//
//   Engine   - the top-level object. Knows the data root directory and which
//              database is currently in use (USE ...). Handles create/drop/list
//              of whole databases.
// -----------------------------------------------------------------------------
#pragma once

#include "minidb/bplustree.h"
#include "minidb/catalog.h"
#include "minidb/table.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace minidb {

// A loaded index together with the column position it indexes.
struct OpenIndex {
    std::string name;
    int         colIndex = -1;
    BPlusTree*  tree = nullptr;
};

class Database {
public:
    explicit Database(std::string dirPath, std::string name);

    const std::string& name() const { return name_; }
    const std::string& path() const { return dirPath_; }
    Catalog&           catalog() { return catalog_; }
    const Catalog&     catalog() const { return catalog_; }

    // Tables -----------------------------------------------------------------
    Table& table(const std::string& name);          // lazily opens the heap file
    void   createTable(const std::string& name, const Schema& schema);
    void   dropTable(const std::string& name);

    // Indexes ----------------------------------------------------------------
    BPlusTree& index(const std::string& indexName); // lazily loads the .idx file
    void       createIndex(const std::string& indexName,
                           const std::string& table,
                           const std::string& column);
    void       dropIndex(const std::string& indexName);

    // Every live index defined on `table`, opened and ready for maintenance.
    std::vector<OpenIndex> openIndexesForTable(const std::string& table);

    // Persist every currently-open (cached) index to its .idx file.
    void flushIndexes();

    void saveCatalog() const;

private:
    std::string dirPath_;
    std::string name_;
    Catalog     catalog_;

    std::map<std::string, std::unique_ptr<Table>>     tableCache_;
    std::map<std::string, std::unique_ptr<BPlusTree>> indexCache_;

    std::string tablePath(const std::string& t) const;
    std::string indexPath(const std::string& i) const;
    std::string catalogPath() const;
};

class Engine {
public:
    explicit Engine(std::string dataRoot);

    const std::string& dataRoot() const { return dataRoot_; }

    void createDatabase(const std::string& name);
    void dropDatabase(const std::string& name);
    void useDatabase(const std::string& name);

    std::vector<std::string> listDatabases() const;
    Database* current() { return current_.get(); }
    const std::string& currentName() const { return currentName_; }

private:
    std::string               dataRoot_;
    std::unique_ptr<Database> current_;
    std::string               currentName_;

    std::string dbPath(const std::string& name) const;
};

} // namespace minidb

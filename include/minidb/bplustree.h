// bplustree.h
// -----------------------------------------------------------------------------
// A B+ tree secondary index mapping a column value -> RID (row location in the
// heap file). It supports duplicate keys (a non-unique index) by ordering
// entries on the composite (key, RID) pair, which keeps every entry unique
// inside the tree while still letting an equality search collect all RIDs for a
// given key by scanning the linked leaf level.
//
// Design note (honest): the tree is maintained in memory using real B+ tree
// node splitting and leaf sibling links. It is persisted by writing its entries
// to an `.idx` file and rebuilt on load. This keeps the algorithmic core - the
// part worth understanding and explaining - genuinely a B+ tree, while leaving
// a fully paged on-disk node layout as a documented future improvement.
// -----------------------------------------------------------------------------
#pragma once

#include "minidb/common.h"

#include <string>
#include <vector>

namespace minidb {

class BPlusTree {
public:
    explicit BPlusTree(ColumnType keyType);
    ~BPlusTree();

    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    // Insert a (key -> rid) mapping. Duplicate keys are allowed.
    void insert(const Value& key, const RID& rid);

    // Remove one exact (key, rid) mapping. No-op if it is not present.
    void remove(const Value& key, const RID& rid);

    // Return every RID whose key equals `key` (index equality seek).
    std::vector<RID> findEqual(const Value& key) const;

    // Persist / rebuild.
    void saveToFile(const std::string& path) const;
    void loadFromFile(const std::string& path);

    // Introspection for STATS / EXPLAIN.
    std::size_t size()   const { return count_; }
    int         height() const;

private:
    struct Entry { Value key; RID rid; };
    struct Node {
        bool                 leaf = true;
        std::vector<Entry>   keys;      // leaf: data entries; internal: separators
        std::vector<Node*>   children;  // internal only
        Node*                next = nullptr; // leaf sibling link
    };

    ColumnType  keyType_;
    Node*       root_ = nullptr;
    std::size_t count_ = 0;

    static bool entryLess(const Entry& a, const Entry& b);

    // Recursive insert; returns true if `node` split, handing back the key to
    // promote and the new right sibling.
    bool insertRec(Node* node, const Entry& e, Entry& promote, Node*& newChild);

    Node* leftmostLeaf() const;
    void  destroy(Node* n);
};

} // namespace minidb

// bplustree.cpp
#include "minidb/bplustree.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace minidb {

// Maximum number of keys/entries a node may hold before it must split.
// A small order makes splits happen early, which is convenient for demos and
// still exercises every code path.
static constexpr std::size_t MAX_KEYS = 4;

BPlusTree::BPlusTree(ColumnType keyType) : keyType_(keyType) {}

BPlusTree::~BPlusTree() { destroy(root_); }

void BPlusTree::destroy(Node* n) {
    if (!n) return;
    if (!n->leaf)
        for (Node* c : n->children) destroy(c);
    delete n;
}

// Order entries by key, breaking ties by RID so every entry is unique.
bool BPlusTree::entryLess(const Entry& a, const Entry& b) {
    int c = compareValues(a.key, b.key);
    if (c != 0) return c < 0;
    if (a.rid.pageId != b.rid.pageId) return a.rid.pageId < b.rid.pageId;
    return a.rid.slotId < b.rid.slotId;
}

// ---------------------------------------------------------------------------
// Insert
// ---------------------------------------------------------------------------
void BPlusTree::insert(const Value& key, const RID& rid) {
    Entry e{key, rid};
    e.key.type = keyType_; // normalize the key's declared type

    if (!root_) {
        root_ = new Node();
        root_->leaf = true;
        root_->keys.push_back(e);
        count_ = 1;
        return;
    }

    Entry promote;
    Node* newChild = nullptr;
    bool split = insertRec(root_, e, promote, newChild);
    if (split) {
        Node* newRoot = new Node();
        newRoot->leaf = false;
        newRoot->keys.push_back(promote);
        newRoot->children.push_back(root_);
        newRoot->children.push_back(newChild);
        root_ = newRoot;
    }
    ++count_;
}

bool BPlusTree::insertRec(Node* node, const Entry& e, Entry& promote, Node*& newChild) {
    if (node->leaf) {
        // Insert into sorted position.
        auto it = std::lower_bound(node->keys.begin(), node->keys.end(), e,
                                   entryLess);
        node->keys.insert(it, e);

        if (node->keys.size() <= MAX_KEYS) return false;

        // Split leaf: right half moves out, its first entry is promoted (copied).
        std::size_t mid = node->keys.size() / 2;
        Node* right = new Node();
        right->leaf = true;
        right->keys.assign(node->keys.begin() + mid, node->keys.end());
        node->keys.erase(node->keys.begin() + mid, node->keys.end());

        right->next = node->next;
        node->next = right;

        promote = right->keys.front();
        newChild = right;
        return true;
    }

    // Internal node: find the child to descend into.
    std::size_t i = 0;
    while (i < node->keys.size() && !entryLess(e, node->keys[i])) ++i;

    Entry childPromote;
    Node* childNew = nullptr;
    bool childSplit = insertRec(node->children[i], e, childPromote, childNew);
    if (!childSplit) return false;

    // Absorb the child's split.
    node->keys.insert(node->keys.begin() + i, childPromote);
    node->children.insert(node->children.begin() + i + 1, childNew);

    if (node->keys.size() <= MAX_KEYS) return false;

    // Split internal node: middle key moves up (not kept in either half).
    std::size_t mid = node->keys.size() / 2;
    promote = node->keys[mid];

    Node* right = new Node();
    right->leaf = false;
    right->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
    right->children.assign(node->children.begin() + mid + 1, node->children.end());

    node->keys.erase(node->keys.begin() + mid, node->keys.end());
    node->children.erase(node->children.begin() + mid + 1, node->children.end());

    newChild = right;
    return true;
}

// ---------------------------------------------------------------------------
// Equality search
// ---------------------------------------------------------------------------
std::vector<RID> BPlusTree::findEqual(const Value& key) const {
    std::vector<RID> out;
    if (!root_) return out;

    Entry probe{key, RID{0, 0}};
    probe.key.type = keyType_;

    // Descend to the leftmost leaf that could contain `key`.
    Node* n = root_;
    while (!n->leaf) {
        std::size_t i = 0;
        while (i < n->keys.size() && !entryLess(probe, n->keys[i])) ++i;
        n = n->children[i];
    }

    // Position at the first entry >= probe, then collect all with matching key,
    // walking across linked leaves.
    std::size_t idx = 0;
    while (idx < n->keys.size() && entryLess(n->keys[idx], probe)) ++idx;

    while (n) {
        for (; idx < n->keys.size(); ++idx) {
            int c = compareValues(n->keys[idx].key, key);
            if (c < 0) continue;      // shouldn't happen after positioning
            if (c > 0) return out;    // passed the key range
            out.push_back(n->keys[idx].rid);
        }
        n = n->next;
        idx = 0;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Remove (no rebalancing - a documented simplification)
// ---------------------------------------------------------------------------
void BPlusTree::remove(const Value& key, const RID& rid) {
    if (!root_) return;
    Entry e{key, rid};
    e.key.type = keyType_;

    Node* n = root_;
    while (!n->leaf) {
        std::size_t i = 0;
        while (i < n->keys.size() && !entryLess(e, n->keys[i])) ++i;
        n = n->children[i];
    }
    for (auto it = n->keys.begin(); it != n->keys.end(); ++it) {
        if (compareValues(it->key, key) == 0 && it->rid == rid) {
            n->keys.erase(it);
            if (count_ > 0) --count_;
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Introspection
// ---------------------------------------------------------------------------
int BPlusTree::height() const {
    int h = 0;
    Node* n = root_;
    while (n) { ++h; if (n->leaf) break; n = n->children.front(); }
    return h;
}

BPlusTree::Node* BPlusTree::leftmostLeaf() const {
    Node* n = root_;
    if (!n) return nullptr;
    while (!n->leaf) n = n->children.front();
    return n;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
namespace {

void writeU32(std::ostream& os, uint32_t v) {
    char b[4]; for (int k = 0; k < 4; ++k) b[k] = static_cast<char>((v >> (8 * k)) & 0xFF);
    os.write(b, 4);
}
void writeU16(std::ostream& os, uint16_t v) {
    char b[2]; b[0] = static_cast<char>(v & 0xFF); b[1] = static_cast<char>((v >> 8) & 0xFF);
    os.write(b, 2);
}
void writeU64(std::ostream& os, uint64_t v) {
    char b[8]; for (int k = 0; k < 8; ++k) b[k] = static_cast<char>((v >> (8 * k)) & 0xFF);
    os.write(b, 8);
}
uint32_t readU32(std::istream& is) {
    unsigned char b[4]; is.read(reinterpret_cast<char*>(b), 4);
    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
           (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
}
uint16_t readU16(std::istream& is) {
    unsigned char b[2]; is.read(reinterpret_cast<char*>(b), 2);
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
}
uint64_t readU64(std::istream& is) {
    unsigned char b[8]; is.read(reinterpret_cast<char*>(b), 8);
    uint64_t v = 0; for (int k = 0; k < 8; ++k) v |= static_cast<uint64_t>(b[k]) << (8 * k);
    return v;
}

void writeKey(std::ostream& os, ColumnType t, const Value& v) {
    switch (t) {
        case ColumnType::INT: {
            uint64_t u; std::memcpy(&u, &v.i, 8); writeU64(os, u); break;
        }
        case ColumnType::FLOAT: {
            uint64_t u; std::memcpy(&u, &v.d, 8); writeU64(os, u); break;
        }
        case ColumnType::TEXT:
        case ColumnType::CHAR: {
            writeU32(os, static_cast<uint32_t>(v.s.size()));
            os.write(v.s.data(), static_cast<std::streamsize>(v.s.size()));
            break;
        }
    }
}

Value readKey(std::istream& is, ColumnType t) {
    Value v; v.type = t;
    switch (t) {
        case ColumnType::INT: {
            uint64_t u = readU64(is); std::memcpy(&v.i, &u, 8); break;
        }
        case ColumnType::FLOAT: {
            uint64_t u = readU64(is); std::memcpy(&v.d, &u, 8); break;
        }
        case ColumnType::TEXT:
        case ColumnType::CHAR: {
            uint32_t n = readU32(is);
            v.s.resize(n);
            if (n) is.read(&v.s[0], n);
            break;
        }
    }
    return v;
}

} // namespace

void BPlusTree::saveToFile(const std::string& path) const {
    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os) throw DBError("cannot write index file: " + path);

    os.write("MDBIDX01", 8);
    writeU32(os, static_cast<uint32_t>(keyType_));

    // Collect entries via the linked leaf level (already in sorted order).
    std::vector<Entry> all;
    for (Node* n = leftmostLeaf(); n; n = n->next)
        all.insert(all.end(), n->keys.begin(), n->keys.end());

    writeU64(os, all.size());
    for (const auto& e : all) {
        writeKey(os, keyType_, e.key);
        writeU32(os, e.rid.pageId);
        writeU16(os, e.rid.slotId);
    }
}

void BPlusTree::loadFromFile(const std::string& path) {
    std::ifstream is(path, std::ios::binary);
    if (!is) return; // no file yet => empty index

    char magic[8];
    is.read(magic, 8);
    if (is.gcount() != 8 || std::strncmp(magic, "MDBIDX01", 8) != 0)
        throw DBError("corrupt or unrecognized index file: " + path);

    ColumnType t = static_cast<ColumnType>(readU32(is));
    keyType_ = t;
    uint64_t count = readU64(is);
    for (uint64_t k = 0; k < count; ++k) {
        Value key = readKey(is, t);
        RID rid;
        rid.pageId = readU32(is);
        rid.slotId = readU16(is);
        insert(key, rid);
    }
}

} // namespace minidb

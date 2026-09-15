// table.h
// -----------------------------------------------------------------------------
// A Table is a heap file: an unordered collection of rows stored in slotted
// pages on top of a Pager. Rows are addressed by RID (page id + slot id). This
// class owns row serialization, slot management, and full-table iteration.
//
// Slotted page layout (per PAGE_SIZE page):
//
//   offset 0   : uint16 slotCount
//   offset 2   : uint16 freePtr        (top of free space; grows downward)
//   offset 4   : slot[0] { uint16 recOffset, uint16 recLen }
//                slot[1] ...           (slot directory grows upward)
//   ...
//   [ free space ]
//   ...
//   record data                        (grows downward from freePtr)
//
// A slot with recLen == 0 is a tombstone (deleted or relocated row).
// -----------------------------------------------------------------------------
#pragma once

#include "minidb/common.h"
#include "minidb/pager.h"

#include <functional>
#include <memory>
#include <string>

namespace minidb {

class Table {
public:
    Table(std::string path, Schema schema);

    const Schema& schema() const { return schema_; }

    // Insert a fully-formed row (already validated/coerced). Returns its RID.
    RID insertRow(const Row& row);

    // Fetch a row by RID. Returns false if the slot is a tombstone.
    bool getRow(const RID& rid, Row& out);

    // Overwrite a row. May relocate it (returns a possibly-new RID).
    RID updateRow(const RID& rid, const Row& row);

    // Tombstone a row.
    void deleteRow(const RID& rid);

    // Visit every live row in the table (full scan).
    void scanAll(const std::function<void(const RID&, const Row&)>& fn);

    // Cheap stats for the STATS command.
    uint32_t pageCount()    { return pager_->numPages(); }
    uint64_t liveRowCount();

    void flush() { pager_->flush(); }

private:
    Schema                 schema_;
    std::unique_ptr<Pager> pager_;

    // Serialize / deserialize a row against the current schema.
    std::vector<uint8_t> serialize(const Row& row) const;
    Row                  deserialize(const uint8_t* data, std::size_t len) const;

    // Page header helpers.
    static uint16_t slotCount(const Page& p);
    static uint16_t freePtr(const Page& p);
    static void     setSlotCount(Page& p, uint16_t v);
    static void     setFreePtr(Page& p, uint16_t v);
    static void     readSlot(const Page& p, uint16_t slot, uint16_t& off, uint16_t& len);
    static void     writeSlot(Page& p, uint16_t slot, uint16_t off, uint16_t len);
    static uint16_t freeSpace(const Page& p);
    static void     initPage(Page& p);
};

} // namespace minidb

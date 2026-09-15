// table.cpp
#include "minidb/table.h"

#include <cstring>

namespace minidb {

// ---------------------------------------------------------------------------
// Little-endian byte helpers. Kept local; the on-disk format is defined here.
// ---------------------------------------------------------------------------
namespace {

void putU32(std::vector<uint8_t>& b, uint32_t v) {
    for (int k = 0; k < 4; ++k) b.push_back(static_cast<uint8_t>((v >> (8 * k)) & 0xFF));
}
void putI64(std::vector<uint8_t>& b, int64_t v) {
    uint64_t u; std::memcpy(&u, &v, 8);
    for (int k = 0; k < 8; ++k) b.push_back(static_cast<uint8_t>((u >> (8 * k)) & 0xFF));
}
void putF64(std::vector<uint8_t>& b, double v) {
    uint64_t u; std::memcpy(&u, &v, 8);
    for (int k = 0; k < 8; ++k) b.push_back(static_cast<uint8_t>((u >> (8 * k)) & 0xFF));
}

uint16_t getU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t getU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
int64_t getI64(const uint8_t* p) {
    uint64_t u = 0;
    for (int k = 0; k < 8; ++k) u |= static_cast<uint64_t>(p[k]) << (8 * k);
    int64_t v; std::memcpy(&v, &u, 8);
    return v;
}
double getF64(const uint8_t* p) {
    uint64_t u = 0;
    for (int k = 0; k < 8; ++k) u |= static_cast<uint64_t>(p[k]) << (8 * k);
    double v; std::memcpy(&v, &u, 8);
    return v;
}

} // namespace

// ---------------------------------------------------------------------------
Table::Table(std::string path, Schema schema)
    : schema_(std::move(schema)),
      pager_(std::make_unique<Pager>(std::move(path))) {}

// ---- Record serialization --------------------------------------------------
std::vector<uint8_t> Table::serialize(const Row& row) const {
    const auto& cols = schema_.columns;
    std::vector<uint8_t> out;

    // Null bitmap.
    std::size_t nbytes = (cols.size() + 7) / 8;
    std::vector<uint8_t> bitmap(nbytes, 0);
    for (std::size_t k = 0; k < cols.size(); ++k) {
        if (row[k].isNull) bitmap[k / 8] |= static_cast<uint8_t>(1u << (k % 8));
    }
    out.insert(out.end(), bitmap.begin(), bitmap.end());

    for (std::size_t k = 0; k < cols.size(); ++k) {
        if (row[k].isNull) continue;
        const Value& v = row[k];
        switch (cols[k].type) {
            case ColumnType::INT:   putI64(out, v.i); break;
            case ColumnType::FLOAT: putF64(out, v.d); break;
            case ColumnType::TEXT: {
                putU32(out, static_cast<uint32_t>(v.s.size()));
                out.insert(out.end(), v.s.begin(), v.s.end());
                break;
            }
            case ColumnType::CHAR: {
                std::string s = v.s;
                s.resize(static_cast<std::size_t>(cols[k].charLen), '\0'); // pad/truncate
                out.insert(out.end(), s.begin(), s.end());
                break;
            }
        }
    }
    return out;
}

Row Table::deserialize(const uint8_t* data, std::size_t len) const {
    const auto& cols = schema_.columns;
    Row row(cols.size());
    std::size_t nbytes = (cols.size() + 7) / 8;
    std::size_t pos = nbytes;

    for (std::size_t k = 0; k < cols.size(); ++k) {
        bool isNull = (data[k / 8] >> (k % 8)) & 1u;
        Value v;
        v.type = cols[k].type;
        if (isNull) { v.isNull = true; row[k] = v; continue; }

        switch (cols[k].type) {
            case ColumnType::INT:
                v.i = getI64(data + pos); pos += 8; break;
            case ColumnType::FLOAT:
                v.d = getF64(data + pos); pos += 8; break;
            case ColumnType::TEXT: {
                uint32_t sl = getU32(data + pos); pos += 4;
                v.s.assign(reinterpret_cast<const char*>(data + pos), sl);
                pos += sl;
                break;
            }
            case ColumnType::CHAR: {
                std::size_t n = static_cast<std::size_t>(cols[k].charLen);
                std::string s(reinterpret_cast<const char*>(data + pos), n);
                // Trim trailing NUL padding for display.
                std::size_t z = s.find('\0');
                if (z != std::string::npos) s.resize(z);
                v.s = s;
                pos += n;
                break;
            }
        }
        row[k] = v;
    }
    (void)len;
    return row;
}

// ---- Page header helpers ---------------------------------------------------
uint16_t Table::slotCount(const Page& p) { return getU16(p.data() + 0); }
uint16_t Table::freePtr(const Page& p)   { return getU16(p.data() + 2); }

void Table::setSlotCount(Page& p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
void Table::setFreePtr(Page& p, uint16_t v) {
    p[2] = static_cast<uint8_t>(v & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void Table::readSlot(const Page& p, uint16_t slot, uint16_t& off, uint16_t& len) {
    const uint8_t* base = p.data() + 4 + slot * 4;
    off = getU16(base);
    len = getU16(base + 2);
}
void Table::writeSlot(Page& p, uint16_t slot, uint16_t off, uint16_t len) {
    uint8_t* base = p.data() + 4 + slot * 4;
    base[0] = static_cast<uint8_t>(off & 0xFF);
    base[1] = static_cast<uint8_t>((off >> 8) & 0xFF);
    base[2] = static_cast<uint8_t>(len & 0xFF);
    base[3] = static_cast<uint8_t>((len >> 8) & 0xFF);
}

uint16_t Table::freeSpace(const Page& p) {
    uint16_t slotDirEnd = static_cast<uint16_t>(4 + slotCount(p) * 4);
    uint16_t fp = freePtr(p);
    if (fp <= slotDirEnd) return 0;
    return static_cast<uint16_t>(fp - slotDirEnd);
}

void Table::initPage(Page& p) {
    p.fill(0);
    setSlotCount(p, 0);
    setFreePtr(p, static_cast<uint16_t>(PAGE_SIZE));
}

// ---- Insert ----------------------------------------------------------------
RID Table::insertRow(const Row& row) {
    std::vector<uint8_t> rec = serialize(row);
    if (rec.size() + 4 + 4 > PAGE_SIZE)
        throw DBError("row too large to fit in a single page");
    uint16_t need = static_cast<uint16_t>(rec.size());

    uint32_t nPages = pager_->numPages();
    Page page;

    // First-fit: find an existing page with room for the record + a new slot.
    for (uint32_t pid = 0; pid < nPages; ++pid) {
        pager_->readPage(pid, page);
        if (freeSpace(page) >= need + 4) {
            uint16_t sc = slotCount(page);
            uint16_t fp = freePtr(page);
            uint16_t recOff = static_cast<uint16_t>(fp - need);
            std::memcpy(page.data() + recOff, rec.data(), rec.size());
            writeSlot(page, sc, recOff, need);
            setSlotCount(page, static_cast<uint16_t>(sc + 1));
            setFreePtr(page, recOff);
            pager_->writePage(pid, page);
            return RID{pid, sc};
        }
    }

    // No room anywhere: allocate a fresh page.
    uint32_t pid = pager_->allocatePage();
    initPage(page);
    uint16_t recOff = static_cast<uint16_t>(PAGE_SIZE - need);
    std::memcpy(page.data() + recOff, rec.data(), rec.size());
    writeSlot(page, 0, recOff, need);
    setSlotCount(page, 1);
    setFreePtr(page, recOff);
    pager_->writePage(pid, page);
    return RID{pid, 0};
}

// ---- Get -------------------------------------------------------------------
bool Table::getRow(const RID& rid, Row& out) {
    if (rid.pageId >= pager_->numPages()) return false;
    Page page;
    pager_->readPage(rid.pageId, page);
    if (rid.slotId >= slotCount(page)) return false;
    uint16_t off, len;
    readSlot(page, rid.slotId, off, len);
    if (len == 0) return false; // tombstone
    out = deserialize(page.data() + off, len);
    return true;
}

// ---- Update ----------------------------------------------------------------
RID Table::updateRow(const RID& rid, const Row& row) {
    std::vector<uint8_t> rec = serialize(row);
    uint16_t need = static_cast<uint16_t>(rec.size());

    Page page;
    pager_->readPage(rid.pageId, page);
    uint16_t off, len;
    readSlot(page, rid.slotId, off, len);
    if (len == 0) throw DBError("cannot update a deleted row");

    // If it still fits in the original gap, overwrite in place.
    if (need <= len) {
        std::memcpy(page.data() + off, rec.data(), rec.size());
        writeSlot(page, rid.slotId, off, need);
        pager_->writePage(rid.pageId, page);
        return rid;
    }

    // Otherwise tombstone here and re-insert elsewhere.
    writeSlot(page, rid.slotId, off, 0);
    pager_->writePage(rid.pageId, page);
    return insertRow(row);
}

// ---- Delete ----------------------------------------------------------------
void Table::deleteRow(const RID& rid) {
    Page page;
    pager_->readPage(rid.pageId, page);
    if (rid.slotId >= slotCount(page)) return;
    uint16_t off, len;
    readSlot(page, rid.slotId, off, len);
    writeSlot(page, rid.slotId, off, 0); // tombstone
    pager_->writePage(rid.pageId, page);
}

// ---- Scan ------------------------------------------------------------------
void Table::scanAll(const std::function<void(const RID&, const Row&)>& fn) {
    uint32_t nPages = pager_->numPages();
    Page page;
    for (uint32_t pid = 0; pid < nPages; ++pid) {
        pager_->readPage(pid, page);
        uint16_t sc = slotCount(page);
        for (uint16_t s = 0; s < sc; ++s) {
            uint16_t off, len;
            readSlot(page, s, off, len);
            if (len == 0) continue;
            Row row = deserialize(page.data() + off, len);
            fn(RID{pid, s}, row);
        }
    }
}

uint64_t Table::liveRowCount() {
    uint64_t count = 0;
    scanAll([&](const RID&, const Row&) { ++count; });
    return count;
}

} // namespace minidb

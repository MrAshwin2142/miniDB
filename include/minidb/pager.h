// pager.h
// -----------------------------------------------------------------------------
// The pager is the lowest storage layer. It treats a file as a contiguous array
// of fixed-size pages (PAGE_SIZE bytes each) and offers read / write / allocate
// operations addressed by page id. Both the table heap files and the B+ tree
// index files sit on top of a pager, so page management logic lives in exactly
// one place.
// -----------------------------------------------------------------------------
#pragma once

#include "minidb/common.h"

#include <array>
#include <fstream>
#include <string>

namespace minidb {

// A single in-memory page: a raw fixed-size byte buffer.
using Page = std::array<uint8_t, PAGE_SIZE>;

class Pager {
public:
    // Opens (creating if necessary) the backing file. Throws DBError on failure.
    explicit Pager(std::string path);
    ~Pager();

    Pager(const Pager&) = delete;
    Pager& operator=(const Pager&) = delete;

    // Number of pages currently in the file (fileSize / PAGE_SIZE).
    uint32_t numPages();

    // Read page `id` into `out`. Reading beyond EOF yields a zero-filled page.
    void readPage(uint32_t id, Page& out);

    // Write `in` to page `id`, growing the file if required.
    void writePage(uint32_t id, const Page& in);

    // Append a brand-new zero-filled page and return its id.
    uint32_t allocatePage();

    // Flush OS buffers to disk.
    void flush();

    const std::string& path() const { return path_; }

private:
    std::string  path_;
    std::fstream file_;

    void ensureOpen();
    uint64_t fileSize();
};

} // namespace minidb

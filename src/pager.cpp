// pager.cpp
#include "minidb/pager.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace minidb {

Pager::Pager(std::string path) : path_(std::move(path)) {
    // Create the file if it does not exist yet, then open for read+write.
    if (!fs::exists(path_)) {
        std::ofstream create(path_, std::ios::binary);
        if (!create) throw DBError("cannot create storage file: " + path_);
    }
    file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_) throw DBError("cannot open storage file: " + path_);
}

Pager::~Pager() {
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

void Pager::ensureOpen() {
    if (!file_.is_open()) throw DBError("storage file is not open: " + path_);
}

uint64_t Pager::fileSize() {
    ensureOpen();
    file_.clear();
    file_.seekg(0, std::ios::end);
    return static_cast<uint64_t>(file_.tellg());
}

uint32_t Pager::numPages() {
    return static_cast<uint32_t>(fileSize() / PAGE_SIZE);
}

void Pager::readPage(uint32_t id, Page& out) {
    ensureOpen();
    out.fill(0);
    uint64_t offset = static_cast<uint64_t>(id) * PAGE_SIZE;
    if (offset >= fileSize()) return; // reading an unallocated page => zeros
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    file_.read(reinterpret_cast<char*>(out.data()), PAGE_SIZE);
    // A short read (partial trailing page) simply leaves the rest zeroed.
    file_.clear();
}

void Pager::writePage(uint32_t id, const Page& in) {
    ensureOpen();
    uint64_t offset = static_cast<uint64_t>(id) * PAGE_SIZE;
    file_.clear();
    file_.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    file_.write(reinterpret_cast<const char*>(in.data()), PAGE_SIZE);
    file_.flush();
}

uint32_t Pager::allocatePage() {
    uint32_t id = numPages();
    Page empty;
    empty.fill(0);
    writePage(id, empty);
    return id;
}

void Pager::flush() {
    if (file_.is_open()) file_.flush();
}

} // namespace minidb

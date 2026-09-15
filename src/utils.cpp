// utils.cpp
#include "minidb/utils.h"
#include "minidb/common.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace minidb {

// ---- ColumnType / Value / RID helpers (declared in common.h) ---------------

std::string columnTypeName(ColumnType t) {
    switch (t) {
        case ColumnType::INT:   return "INT";
        case ColumnType::FLOAT: return "FLOAT";
        case ColumnType::TEXT:  return "TEXT";
        case ColumnType::CHAR:  return "CHAR";
    }
    return "?";
}

std::string Value::toString() const {
    if (isNull) return "NULL";
    switch (type) {
        case ColumnType::INT:   return std::to_string(i);
        case ColumnType::FLOAT: {
            std::ostringstream os;
            os << d;
            return os.str();
        }
        case ColumnType::TEXT:
        case ColumnType::CHAR:  return s;
    }
    return "?";
}

static bool isNumeric(ColumnType t) {
    return t == ColumnType::INT || t == ColumnType::FLOAT;
}

int compareValues(const Value& a, const Value& b) {
    // NULLs sort first and compare equal to each other.
    if (a.isNull || b.isNull) {
        if (a.isNull && b.isNull) return 0;
        return a.isNull ? -1 : 1;
    }

    if (isNumeric(a.type) && isNumeric(b.type)) {
        // Compare numerically. Promote to double only when a float is involved
        // so integer comparisons stay exact.
        if (a.type == ColumnType::INT && b.type == ColumnType::INT) {
            if (a.i < b.i) return -1;
            if (a.i > b.i) return 1;
            return 0;
        }
        double av = (a.type == ColumnType::INT) ? static_cast<double>(a.i) : a.d;
        double bv = (b.type == ColumnType::INT) ? static_cast<double>(b.i) : b.d;
        if (av < bv) return -1;
        if (av > bv) return 1;
        return 0;
    }

    // Fall back to string comparison for TEXT/CHAR.
    if (a.s < b.s) return -1;
    if (a.s > b.s) return 1;
    return 0;
}

std::string RID::toString() const {
    return "(" + std::to_string(pageId) + ":" + std::to_string(slotId) + ")";
}

int Schema::columnIndex(const std::string& name) const {
    for (std::size_t k = 0; k < columns.size(); ++k) {
        // Column names are case-insensitive, values are not.
        if (util::iequals(columns[k].name, name)) return static_cast<int>(k);
    }
    return -1;
}

const Column* Schema::column(const std::string& name) const {
    int idx = columnIndex(name);
    return idx < 0 ? nullptr : &columns[static_cast<std::size_t>(idx)];
}

// ---- util namespace --------------------------------------------------------
namespace util {

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t k = 0; k < a.size(); ++k) {
        if (std::tolower(static_cast<unsigned char>(a[k])) !=
            std::tolower(static_cast<unsigned char>(b[k])))
            return false;
    }
    return true;
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream is(s);
    while (std::getline(is, cur, delim)) out.push_back(cur);
    return out;
}

std::string repeat(const std::string& s, int n) {
    std::string out;
    for (int k = 0; k < n; ++k) out += s;
    return out;
}

std::string padRight(const std::string& s, std::size_t width) {
    if (s.size() >= width) return s;
    return s + std::string(width - s.size(), ' ');
}

} // namespace util
} // namespace minidb

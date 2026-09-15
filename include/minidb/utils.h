// utils.h
// -----------------------------------------------------------------------------
// Small, dependency-free string and formatting helpers used across the engine.
// -----------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

namespace minidb {
namespace util {

std::string toUpper(std::string s);
std::string toLower(std::string s);
std::string trim(const std::string& s);

// Case-insensitive equality (used for keyword matching).
bool iequals(const std::string& a, const std::string& b);

// Split on a single delimiter.
std::vector<std::string> split(const std::string& s, char delim);

// Repeat a string n times (used to draw table separators).
std::string repeat(const std::string& s, int n);

// Pad a string on the right to `width` with spaces (left-justify).
std::string padRight(const std::string& s, std::size_t width);

} // namespace util
} // namespace minidb

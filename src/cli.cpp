// cli.cpp
#include "minidb/cli.h"
#include "minidb/lexer.h"
#include "minidb/parser.h"
#include "minidb/utils.h"

#include <fstream>
#include <iostream>
#include <sstream>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace minidb {

// ANSI color codes (Windows Terminal / PowerShell support these once virtual
// terminal processing is enabled).
namespace col {
constexpr const char* RESET  = "\033[0m";
constexpr const char* CYAN   = "\033[36m";
constexpr const char* GREEN  = "\033[32m";
constexpr const char* RED    = "\033[31m";
constexpr const char* YELLOW = "\033[33m";
constexpr const char* GREY   = "\033[90m";
}

static void enableWindowsAnsi() {
#if defined(_WIN32)
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if (!GetConsoleMode(h, &mode)) return;
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

Cli::Cli(Engine& engine) : engine_(engine), executor_(engine) {
    enableWindowsAnsi();
}

std::string Cli::colorize(const std::string& s, const char* code) const {
    if (!color_) return s;
    return std::string(code) + s + col::RESET;
}

void Cli::printBanner() {
    std::cout << colorize(
        "  __  __ _       _ ____  ____\n"
        " |  \\/  (_)_ __ (_)  _ \\| __ )\n"
        " | |\\/| | | '_ \\| | | | |  _ \\\n"
        " | |  | | | | | | | |_| | |_) |\n"
        " |_|  |_|_|_| |_|_|____/|____/\n", col::CYAN);
    std::cout << " miniDB " << "1.0"
              << " - a file-based relational database engine in C++\n";
    std::cout << colorize(" storage : ", col::GREY) << engine_.dataRoot() << "\n";
    std::cout << colorize(" database: ", col::GREY)
              << (engine_.current() ? engine_.currentName() : "(none - run USE <db>;)") << "\n";
    std::cout << colorize(" Type HELP; for commands, EXIT; to quit.\n", col::GREY);
    std::cout << std::endl;
}

std::string Cli::prompt() const {
    std::string p = "miniDB";
    if (engine_.current()) p += " [" + engine_.currentName() + "]";
    p += " > ";
    return colorize(p, col::CYAN);
}

// ---------------------------------------------------------------------------
// Statement splitting
// ---------------------------------------------------------------------------
std::vector<std::string> Cli::splitStatements(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    bool inStr = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (inStr) {
            cur += c;
            if (c == '\\' && i + 1 < text.size()) { cur += text[++i]; continue; }
            if (c == '\'') inStr = false;
            continue;
        }
        if (c == '\'') { inStr = true; cur += c; continue; }
        // Line comment: -- ... newline
        if (c == '-' && i + 1 < text.size() && text[i + 1] == '-') {
            while (i < text.size() && text[i] != '\n') ++i;
            cur += '\n';
            continue;
        }
        if (c == ';') {
            if (!util::trim(cur).empty()) out.push_back(util::trim(cur));
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (!util::trim(cur).empty()) out.push_back(util::trim(cur));
    return out;
}

// ---------------------------------------------------------------------------
// Execute one statement
// ---------------------------------------------------------------------------
bool Cli::executeStatement(const std::string& sql, bool echo) {
    if (echo) std::cout << colorize("sql> ", col::GREY) << sql << ";\n";
    try {
        Lexer lexer(sql);
        Parser parser(lexer.tokenize());
        StmtPtr stmt = parser.parseStatement();

        Result r = executor_.execute(*stmt);

        if (r.quit) {
            std::cout << colorize(r.message, col::GREEN) << "\n";
            return false;
        }
        if (!r.execFile.empty()) {
            std::cout << colorize("Running script: ", col::YELLOW) << r.execFile << "\n";
            runScriptFile(r.execFile, /*echo=*/true);
            return true;
        }
        printResult(r);
    } catch (const DBError& e) {
        std::cout << colorize(std::string("Error: ") + e.what(), col::RED) << "\n";
    } catch (const std::exception& e) {
        std::cout << colorize(std::string("Internal error: ") + e.what(), col::RED) << "\n";
    }
    return true;
}

// ---------------------------------------------------------------------------
// Result printing
// ---------------------------------------------------------------------------
void Cli::printResult(const Result& r) {
    if (!r.plan.empty())
        std::cout << colorize(r.plan, col::YELLOW) << "\n";

    if (r.isResultSet) {
        printTable(r.columns, r.rows);
        std::cout << colorize(r.message + "  (" +
                              [](double ms){ std::ostringstream o; o.precision(3);
                                             o << std::fixed << ms; return o.str(); }(r.elapsedMs) +
                              " ms)", col::GREY) << "\n";
    } else if (!r.message.empty()) {
        std::cout << colorize(r.message, col::GREEN) << "\n";
    }
}

void Cli::printTable(const std::vector<std::string>& cols,
                     const std::vector<std::vector<std::string>>& rows) {
    std::vector<std::size_t> width(cols.size());
    for (std::size_t k = 0; k < cols.size(); ++k) width[k] = cols[k].size();
    for (const auto& row : rows)
        for (std::size_t k = 0; k < row.size() && k < width.size(); ++k)
            width[k] = std::max(width[k], row[k].size());

    auto border = [&]() {
        std::string line = "+";
        for (std::size_t k = 0; k < width.size(); ++k)
            line += util::repeat("-", static_cast<int>(width[k] + 2)) + "+";
        return line;
    };

    std::cout << border() << "\n";
    std::string header = "|";
    for (std::size_t k = 0; k < cols.size(); ++k)
        header += " " + util::padRight(cols[k], width[k]) + " |";
    std::cout << colorize(header, col::CYAN) << "\n";
    std::cout << border() << "\n";

    for (const auto& row : rows) {
        std::string line = "|";
        for (std::size_t k = 0; k < width.size(); ++k) {
            std::string cell = (k < row.size()) ? row[k] : "";
            line += " " + util::padRight(cell, width[k]) + " |";
        }
        std::cout << line << "\n";
    }
    std::cout << border() << "\n";
}

// ---------------------------------------------------------------------------
// Script + REPL drivers
// ---------------------------------------------------------------------------
bool Cli::runScriptFile(const std::string& path, bool echo) {
    std::ifstream in(path);
    if (!in) {
        std::cout << colorize("Error: cannot open script file: " + path, col::RED) << "\n";
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    for (const auto& stmt : splitStatements(ss.str())) {
        if (!executeStatement(stmt, echo)) return true; // EXIT inside script
    }
    return true;
}

int Cli::runInteractive() {
    printBanner();
    std::string buffer;
    std::string line;

    std::cout << prompt() << std::flush;
    while (std::getline(std::cin, line)) {
        buffer += line + "\n";

        // Execute every complete (semicolon-terminated) statement in the buffer.
        // Anything after the last ';' stays buffered for the next line.
        std::size_t lastSemi = buffer.rfind(';');
        if (lastSemi != std::string::npos) {
            std::string ready = buffer.substr(0, lastSemi + 1);
            std::string rest = buffer.substr(lastSemi + 1);
            buffer = rest;
            bool keepGoing = true;
            for (const auto& stmt : splitStatements(ready)) {
                if (!executeStatement(stmt, /*echo=*/false)) { keepGoing = false; break; }
            }
            if (!keepGoing) return 0;
        }
        std::cout << prompt() << std::flush;
    }
    std::cout << "\n";
    return 0;
}

} // namespace minidb

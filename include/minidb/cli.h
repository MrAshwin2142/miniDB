// cli.h
// -----------------------------------------------------------------------------
// The interactive shell. It owns the read-eval-print loop, the startup banner,
// statement splitting, result formatting (aligned ASCII tables), and running
// .sql script files (both from the command line and via EXEC).
// -----------------------------------------------------------------------------
#pragma once

#include "minidb/database.h"
#include "minidb/executor.h"

#include <string>
#include <vector>

namespace minidb {

class Cli {
public:
    explicit Cli(Engine& engine);

    // Interactive REPL. Returns process exit code.
    int runInteractive();

    // Execute every statement in a .sql file. Returns true if it ran without a
    // fatal I/O error (individual statement errors are reported and skipped).
    bool runScriptFile(const std::string& path, bool echo);

private:
    Engine&  engine_;
    Executor executor_;
    bool     color_ = true;

    void printBanner();
    std::string prompt() const;

    // Split a chunk of text into individual statements on top-level ';',
    // respecting quoted strings and -- line comments.
    static std::vector<std::string> splitStatements(const std::string& text);

    // Parse + execute one statement string and print the outcome. Returns false
    // if the engine asked to quit (EXIT/QUIT).
    bool executeStatement(const std::string& sql, bool echo);

    void printResult(const Result& r);
    void printTable(const std::vector<std::string>& cols,
                    const std::vector<std::vector<std::string>>& rows);

    std::string colorize(const std::string& s, const char* code) const;
};

} // namespace minidb

// test_basic.cpp
// -----------------------------------------------------------------------------
// A lightweight, dependency-free test driver. It runs SQL through the real
// lexer -> parser -> executor pipeline against a throwaway data directory and
// checks the Results. Run the `minidb_tests` executable; a non-zero exit code
// means something failed.
// -----------------------------------------------------------------------------
#include "minidb/database.h"
#include "minidb/executor.h"
#include "minidb/lexer.h"
#include "minidb/parser.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace minidb;

static int g_checks = 0;
static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) {
        std::cout << "  ok   : " << what << "\n";
    } else {
        ++g_failures;
        std::cout << "  FAIL : " << what << "\n";
    }
}

// Run one SQL statement and return its Result.
static Result run(Executor& ex, const std::string& sql) {
    Lexer lexer(sql);
    Parser parser(lexer.tokenize());
    StmtPtr stmt = parser.parseStatement();
    return ex.execute(*stmt);
}

int main() {
    const std::string dir = "test_data";
    std::error_code ec;
    fs::remove_all(dir, ec); // start clean

    std::cout << "== miniDB test suite ==\n";

    {
        Engine engine(dir);
        Executor ex(engine);

        run(ex, "CREATE DATABASE shop");
        run(ex, "USE shop");
        run(ex, "CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT)");

        run(ex, "INSERT INTO users VALUES (1, 'Alice', 30)");
        run(ex, "INSERT INTO users VALUES (2, 'Bob', 25)");
        run(ex, "INSERT INTO users VALUES (3, 'Carol', 30)");
        run(ex, "INSERT INTO users (id, name, age) VALUES (4, 'Dave', 40)");

        Result all = run(ex, "SELECT * FROM users");
        check(all.rows.size() == 4, "SELECT * returns 4 rows");

        Result where = run(ex, "SELECT name FROM users WHERE age = 30");
        check(where.rows.size() == 2, "WHERE age=30 returns 2 rows (full scan)");

        // Primary key uniqueness.
        bool threw = false;
        try { run(ex, "INSERT INTO users VALUES (1, 'Eve', 22)"); }
        catch (const DBError&) { threw = true; }
        check(threw, "duplicate primary key is rejected");

        // Type checking.
        threw = false;
        try { run(ex, "INSERT INTO users VALUES (5, 'Frank', 'notanint')"); }
        catch (const DBError&) { threw = true; }
        check(threw, "type mismatch is rejected");

        // Index seek.
        run(ex, "CREATE INDEX idx_age ON users(age)");
        Result seek = run(ex, "SELECT name FROM users WHERE age = 30");
        check(seek.rows.size() == 2, "WHERE age=30 returns 2 rows (index seek)");

        Result seek2 = run(ex, "SELECT id FROM users WHERE age = 40");
        check(seek2.rows.size() == 1 && seek2.rows[0][0] == "4",
              "index seek finds the single age=40 row");

        // Aggregates.
        Result cnt = run(ex, "SELECT COUNT(*) FROM users");
        check(cnt.rows[0][0] == "4", "COUNT(*) = 4");
        Result sum = run(ex, "SELECT SUM(age) FROM users");
        check(sum.rows[0][0] == "125", "SUM(age) = 125");

        // ORDER BY + LIMIT.
        Result ord = run(ex, "SELECT id FROM users ORDER BY age DESC LIMIT 1");
        check(ord.rows.size() == 1 && ord.rows[0][0] == "4",
              "ORDER BY age DESC LIMIT 1 gives id 4");

        // UPDATE (also keeps the index consistent).
        run(ex, "UPDATE users SET age = 31 WHERE id = 1");
        Result afterUpd = run(ex, "SELECT id FROM users WHERE age = 31");
        check(afterUpd.rows.size() == 1 && afterUpd.rows[0][0] == "1",
              "UPDATE moves row to age=31 and index reflects it");
        Result old30 = run(ex, "SELECT id FROM users WHERE age = 30");
        check(old30.rows.size() == 1, "old age=30 bucket now has 1 row after update");

        // DELETE.
        run(ex, "DELETE FROM users WHERE id = 2");
        Result afterDel = run(ex, "SELECT COUNT(*) FROM users");
        check(afterDel.rows[0][0] == "3", "COUNT(*) = 3 after delete");
    }

    // Persistence: reopen the engine on the same directory.
    {
        Engine engine(dir);
        Executor ex(engine);
        run(ex, "USE shop");
        Result cnt = run(ex, "SELECT COUNT(*) FROM users");
        check(cnt.rows[0][0] == "3", "data persists across engine restart");
        Result seek = run(ex, "SELECT id FROM users WHERE age = 40");
        check(seek.rows.size() == 1 && seek.rows[0][0] == "4",
              "index persists and still seeks after restart");
    }

    fs::remove_all(dir, ec);

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed.\n";
    if (g_failures) { std::cout << g_failures << " FAILURE(S)\n"; return 1; }
    std::cout << "All tests passed.\n";
    return 0;
}

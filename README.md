# miniDB

A small relational database engine written from scratch in C++17. It stores data in files, speaks a subset of SQL, and does the things a real database does under the hood: it keeps a schema catalog, serializes rows into paged storage, parses queries with a hand-written lexer and parser, runs them through an execution engine that picks between a full table scan and a B+ tree index seek, and persists everything to disk so your data survives a restart.

It is deliberately not an ORM wrapper or a CRUD app over a `std::map`. The point was to understand how the layers of a database actually fit together, and to end up with something I can open in a terminal and demo.

```
  __  __ _       _ ____  ____
 |  \/  (_)_ __ (_)  _ \| __ )
 | |\/| | | '_ \| | | | |  _ \
 | |  | | | | | | | |_| | |_) |
 |_|  |_|_|_| |_|_|____/|____/
 miniDB 1.0 - a file-based relational database engine in C++
```

## Why I built this

I wanted to stop treating the database as a black box. Reading about pages, catalogs and B+ trees only gets you so far; you learn the material differently once you have to make an `INSERT` actually land bytes on a page and then find them again through an index. miniDB is the result of building each layer myself and wiring them together into one working engine.

## Features

- **Databases and tables** — `CREATE DATABASE`, `USE`, `CREATE TABLE`, `DROP`, `SHOW`, `DESCRIBE`.
- **Typed schema** — `INT`, `FLOAT`, `TEXT`, and `CHAR(n)` columns, with `PRIMARY KEY` support, duplicate-column rejection, and per-value type validation.
- **CRUD** — `INSERT`, `SELECT`, `UPDATE`, `DELETE`.
- **Query features** — column projection or `*`, `WHERE` with `= != < <= > >=` and `AND`, `ORDER BY`, `LIMIT`, and the aggregates `COUNT`, `SUM`, `AVG`, `MIN`, `MAX`.
- **Persistent B+ tree indexes** — `CREATE INDEX name ON table(column)`. Equality lookups use the index; the index is maintained automatically on insert, update and delete.
- **Query planning visibility** — `EXPLAIN` shows the chosen access path (full scan vs. index seek) and the rows examined/matched; `TRACE ON` prints the same for every query; each statement reports its wall-clock time.
- **Engine introspection** — `SHOW CATALOG`, `SHOW INDEXES`, `STATS`.
- **Scripting** — run a `.sql` file from the command line, or `EXEC 'file.sql'` from inside the shell.
- **A usable shell** — startup banner, database-aware prompt, aligned table output, readable errors, and colored output on Windows Terminal.

## Architecture

Everything flows through the same pipeline, one layer handing off to the next:

```
input text
   -> Lexer        turns characters into tokens
   -> Parser       turns tokens into a Statement (AST)
   -> Executor     validates against the catalog, picks an access path
   -> Storage      Table heap (slotted pages) + B+ tree index, both on a Pager
   -> Formatter    prints an aligned result table
```

The modules mirror that flow:

| Layer | Files | Responsibility |
|-------|-------|----------------|
| CLI | `cli.*`, `main.cpp` | REPL, banner, script runner, table formatting |
| Lexer | `lexer.*` | Tokenize SQL text |
| Parser / AST | `parser.*`, `ast.h` | Recursive-descent parser producing statement objects |
| Catalog | `catalog.*` | Schema + index metadata, persisted to `catalog.meta` |
| Executor | `executor.*` | Semantic checks, access-path choice, query execution |
| Storage | `pager.*`, `table.*` | Paged file I/O and slotted-page row storage |
| Index | `bplustree.*` | B+ tree mapping column values to row locations |
| Engine | `database.*` | Ties a database's catalog, tables and indexes together |

## Project structure

```
miniDB/
├── CMakeLists.txt
├── README.md
├── guid.md                 # detailed personal guide (setup, internals, demo, interview notes)
├── .gitignore
├── docs/
│   └── architecture.md
├── scripts/
│   ├── setup.sql           # creates the sample "shop" database
│   ├── demo.sql            # guided tour of the query features
│   └── indexing_demo.sql   # full scan vs. index seek, with EXPLAIN/TRACE
├── include/minidb/         # public headers
│   ├── common.h  utils.h  lexer.h  ast.h  parser.h
│   ├── pager.h  table.h  bplustree.h  catalog.h  database.h
│   ├── executor.h  cli.h
├── src/                    # implementations
│   ├── main.cpp  cli.cpp  lexer.cpp  parser.cpp  utils.cpp
│   ├── pager.cpp  table.cpp  bplustree.cpp  catalog.cpp
│   ├── database.cpp  executor.cpp
└── tests/
    └── test_basic.cpp      # end-to-end checks through the real pipeline
```

## How storage works

Each database is a folder under the data root. Inside it:

- `catalog.meta` — a small text file holding every table's schema and every index definition. It is loaded on startup so the engine knows what exists.
- `<table>.tbl` — the table's heap file. It is managed as an array of fixed 4 KB **pages**. Each page uses a **slotted layout**: a tiny header, a slot directory that grows down from the top, and record data that grows up from the bottom. A row is addressed by a **RID** (page id + slot id). Rows are serialized with a null bitmap followed by each column's bytes (fixed width for `INT`/`FLOAT`/`CHAR`, length-prefixed for `TEXT`). Deletes leave a tombstone in the slot.
- `<index>.idx` — a persisted B+ tree.

The `Pager` is the only component that touches the file directly; both the table heap and the index sit on top of it. That is exactly how a real engine keeps its I/O logic in one place.

## How indexing works

An index is a B+ tree that maps a column value to the RIDs of the rows that hold it. Because a non-primary column can repeat, entries are ordered on the composite `(key, RID)` pair — that keeps every entry unique inside the tree while still letting an equality search collect *all* matching RIDs by walking the linked leaf level. Inserts split nodes the usual B+ tree way, and leaves are chained so a range of equal keys can be scanned in order.

When you run `SELECT ... WHERE col = value` and `col` has an index, the executor seeks the tree, gets a handful of RIDs, and jumps straight to those rows instead of scanning the whole table. `EXPLAIN` and `TRACE` let you watch that decision happen and see the drop in rows examined.

One honest note on the current design: the tree is built and mutated in memory using real B+ tree logic, and persisted by writing its entries to the `.idx` file (and rebuilt on load). A fully paged, node-per-page on-disk tree is the natural next step — see [Future improvements](#future-improvements).

## Supported SQL

```sql
CREATE DATABASE shop;
USE shop;
CREATE TABLE customers (id INT PRIMARY KEY, name TEXT, city TEXT, age INT);
INSERT INTO customers VALUES (1, 'Alice', 'Delhi', 30);
INSERT INTO customers (id, name) VALUES (2, 'Bob');

SELECT * FROM customers;
SELECT name, age FROM customers WHERE age >= 30 AND city = 'Delhi' ORDER BY age DESC LIMIT 5;
SELECT COUNT(*), AVG(age) FROM customers;

UPDATE customers SET city = 'Pune' WHERE id = 2;
DELETE FROM customers WHERE age < 18;

CREATE INDEX idx_city ON customers(city);
EXPLAIN SELECT name FROM customers WHERE city = 'Delhi';

SHOW DATABASES;  SHOW TABLES;  SHOW INDEXES;  SHOW CATALOG;
DESCRIBE customers;  STATS;  TRACE ON;  HELP;
EXEC 'scripts/demo.sql';
EXIT;
```

Types are `INT`, `FLOAT`, `TEXT` (aliases `STRING`, `VARCHAR(n)`) and `CHAR(n)`. Comparison operators are `= != < <= > >=`. Statements end with `;`. Line comments start with `--`.

## Getting started on Windows

miniDB is built and used from a terminal. These instructions assume Windows 10/11 with Windows Terminal or PowerShell.

### Prerequisites

You need a C++17 compiler and CMake.

1. **Visual Studio Build Tools 2022** (free). During install, tick the **"Desktop development with C++"** workload. That gives you the MSVC compiler (`cl.exe`) and the CMake that ships with it. Installing full Visual Studio Community works too.
2. Open a shell that has the compiler on its `PATH`. The reliable option is **"Developer PowerShell for VS 2022"** (search it in the Start menu). A plain PowerShell window will not find `cl.exe` unless you have configured it yourself.

Verify the toolchain:

```powershell
cl
cmake --version
```

`cl` should print a Microsoft compiler banner (an error about no input files is fine — it means it was found). `cmake --version` should print 3.16 or newer.

### Build

From the project root, in Developer PowerShell:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

The executable lands at `build\Release\minidb.exe`.

### Run it

Interactive shell:

```powershell
.\build\Release\minidb.exe
```

Load the sample database and explore:

```powershell
.\build\Release\minidb.exe --run scripts\setup.sql
.\build\Release\minidb.exe scripts\demo.sql
.\build\Release\minidb.exe scripts\indexing_demo.sql
```

By default miniDB stores data in a `data\` folder in your current directory. Point it somewhere else with `--data`:

```powershell
.\build\Release\minidb.exe --data C:\Users\you\minidb-data
```

### Run the tests

```powershell
.\build\Release\minidb_tests.exe
```

It runs a set of end-to-end checks through the real lexer/parser/executor and prints a pass/fail summary.

## Example session

```
miniDB [shop] > SELECT name FROM customers WHERE city = 'Delhi';
+-------+
| name  |
+-------+
| Alice |
| Carol |
| Frank |
+-------+
3 row(s).  (0.041 ms)

miniDB [shop] > EXPLAIN SELECT name FROM customers WHERE city = 'Delhi';
Query Plan
----------
Statement    : SELECT
Table        : customers
Access path  : Index seek (idx_city on city)
Index used   : idx_city
Rows examined: 3
Rows matched : 3
```

*(A couple of terminal screenshots go well here — a `SELECT` result table and the `EXPLAIN` before/after adding an index.)*

## Cross-platform note

The code is standard C++17 and also builds on Linux/macOS with GCC or Clang. The only platform-specific code is enabling ANSI colors on Windows; it is guarded so it compiles everywhere. Windows is the primary target and the instructions above are the supported path.

## Current limitations

miniDB is a learning engine, and it is honest about what it is not:

- **Single table queries only** — no `JOIN`, subqueries, or `GROUP BY`.
- **`WHERE` is a conjunction of simple `column OP literal` predicates** — no `OR`, no expressions, no functions in predicates.
- **No transactions** — there is no `BEGIN`/`COMMIT`/`ROLLBACK`, no write-ahead log, and no crash recovery beyond "the last flushed write is what you get."
- **Not concurrent** — one process, one connection, no locking.
- **Space is not reclaimed** — deleted rows leave tombstones; there is no page compaction or vacuum yet.
- **The B+ tree persists by serializing its entries**, not yet as a paged on-disk structure (the in-memory tree is a genuine B+ tree).

None of these are hidden — they are the roadmap.

## Future improvements

- A paged, node-per-page B+ tree with a buffer pool so the index never has to be fully rebuilt.
- Transactions with a write-ahead log and crash recovery.
- `JOIN` and `GROUP BY`, and a cost-based choice between access paths.
- Free-space management and page compaction.
- CSV import/export and a larger regression test suite with benchmarks.

## Resume summary

Built a file-based relational database engine in C++17 with a hand-written SQL lexer and recursive-descent parser, a persistent schema catalog, slotted-page heap storage over a custom pager, a B+ tree index with an execution engine that chooses between full scans and index seeks, plus `EXPLAIN`/`TRACE` observability and a scriptable terminal shell.

## License

MIT. See `LICENSE` if included, or treat the code as MIT-licensed for personal and educational use.

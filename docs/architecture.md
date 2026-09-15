# miniDB architecture

This document explains how the engine is put together and what happens as a statement travels through it. It complements the header comments in the source, which describe each module in place.

## The pipeline

A statement always follows the same path:

```
raw SQL string
  │
  ▼  Lexer            characters  -> tokens
  ▼  Parser           tokens      -> Statement (AST node)
  ▼  Executor         validate names/types, choose access path
  ▼  Storage / Index  read or write bytes
  ▼  Result           columns + rows + timing + plan
  ▼  CLI formatter    aligned ASCII table
```

Each stage has a single responsibility and knows nothing about the stages on either side of it. The lexer does not know what a `SELECT` is; the parser does not know what a page is; the storage layer does not know what SQL is.

## Layers

### Lexer (`lexer.*`)

A straightforward scanner. It recognizes identifiers, keywords (matched case-insensitively and normalized to upper case), integer and float literals, single-quoted strings (with `\` escapes), the comparison operators, and the punctuation symbols. `--` starts a line comment. The output is a flat `std::vector<Token>` ending in an `End` sentinel.

### Parser and AST (`parser.*`, `ast.h`)

A hand-written recursive-descent parser. `parseStatement()` dispatches on the first keyword to a dedicated function per command (`parseSelect`, `parseInsert`, and so on). Each produces a concrete `Statement` subclass carrying a `StmtKind` tag, so the executor can `switch` on the tag and `static_cast` to the right type without RTTI. Keeping one parse function per statement means the grammar is readable top to bottom.

### Catalog (`catalog.*`)

The data dictionary. It holds `TableMeta` (name + `Schema`) and `IndexMeta` (name, table, column) and serializes them to a human-readable `catalog.meta` file. The format is intentionally greppable:

```
TABLE customers 4 0
COL id 0 0 1
COL name 2 0 0
...
INDEX idx_city customers city
```

`TABLE name ncols pkIndex`, then one `COL name typeInt charLen pkFlag` per column, then `INDEX name table column`. Loading the catalog on startup is what makes a database "exist."

### Pager (`pager.*`)

The lowest storage layer. It presents a file as an array of fixed 4 KB pages and offers `readPage`, `writePage`, and `allocatePage`, addressed by page id. Reading an unallocated page returns zeros; writing past the end grows the file. Both heap files and index files are built on a pager, so all raw file I/O lives here.

### Table heap (`table.*`)

A table is a heap file of **slotted pages**. Page layout:

```
0      slotCount (u16)
2      freePtr   (u16)   top of free space, grows downward
4      slot[0] = { recOffset u16, recLen u16 }
6      slot[1] ...                          (slot directory grows upward)
...
[ free space ]
...
record bytes                                (grow downward from freePtr)
```

A **RID** is `(pageId, slotId)`. A slot with `recLen == 0` is a tombstone. Rows serialize as a null bitmap followed by each non-null column: `INT`/`FLOAT` as 8 fixed bytes, `CHAR(n)` as `n` padded bytes, `TEXT` as a 4-byte length plus bytes.

`insertRow` uses first-fit across existing pages and allocates a new page only when nothing has room. `updateRow` overwrites in place when the new record fits the old slot, otherwise tombstones and re-inserts (returning a new RID). `deleteRow` tombstones. `scanAll` iterates every live slot — this is the full table scan.

### B+ tree index (`bplustree.*`)

A B+ tree keyed on a column value, valued by RID. Entries are ordered on the composite `(key, RID)` pair so duplicate keys are supported: every entry is unique in the tree, and an equality search finds the leftmost matching entry then walks the linked leaf level collecting every RID with that key. Internal nodes split by promoting a middle key; leaves split by copying the right half out and chaining `next` pointers.

Persistence is by writing all entries (in sorted leaf order) to the `.idx` file and rebuilding the tree on load. The algorithm — splitting, searching, leaf chaining — is a real B+ tree; the on-disk representation is the simplification, and swapping in a paged node layout is the documented next step.

### Engine and Database (`database.*`)

`Database` represents one open database directory. It owns the `Catalog` and lazily opens `Table` and `BPlusTree` handles, caching them (which also matters on Windows, where an open file cannot be deleted — drops close the handle first). `Engine` sits above it and handles whole-database operations (`CREATE`/`DROP`/`USE`/`SHOW DATABASES`) plus which database is current.

### Executor (`executor.*`)

The brain. It validates that tables and columns exist and that literals match column types (`coerce`), chooses an access path (`choosePlan` — an equality predicate on an indexed column becomes an index seek, otherwise a full scan), gathers matching rows (`gatherRows`), then applies `ORDER BY`, `LIMIT`, projection or aggregation. Writes go through the same gather step and then keep every affected index in sync. It returns a `Result` with columns, rows, a message, timing, and (for `EXPLAIN`/`TRACE`) a plan string.

### CLI (`cli.*`, `main.cpp`)

The shell. It reads input, splits it into statements on top-level `;` (respecting quoted strings and comments), runs each through the pipeline, and prints an aligned table or a status message. It also runs `.sql` files, both from the command line and via `EXEC`.

## Worked example: `SELECT name FROM customers WHERE city = 'Delhi'`

1. **Lexer** produces: `SELECT`, `name`, `FROM`, `customers`, `WHERE`, `city`, `=`, `'Delhi'`.
2. **Parser** builds a `SelectStmt` with one projected column, one equality condition.
3. **Executor** looks up `customers` in the catalog, checks `name` and `city` exist, and calls `choosePlan`. If `city` has an index, the plan is an index seek.
4. **Index seek**: the B+ tree for `city` is searched for `'Delhi'`, returning a list of RIDs.
5. **Heap fetch**: each RID is turned into a row via `Table::getRow`; the `WHERE` is re-checked (cheap and safe).
6. **Project** the `name` column, wrap in a `Result`, and hand it to the CLI, which prints the table and the elapsed time.

Without the index, step 4 becomes `Table::scanAll`, examining every row. `EXPLAIN` reports which path was taken and how many rows each examined.

## Design choices worth calling out

- **One exception type (`DBError`)** for all user-facing errors, caught at the CLI boundary, so a bad query never crashes the process.
- **RIDs, not pointers**, connect indexes to rows — the same indirection real engines use so an index entry stays valid as pages move on disk.
- **Composite `(key, RID)` ordering** is the trick that makes a non-unique B+ tree index behave cleanly without special-casing duplicate keys.
- **Lazy, cached handles** in `Database` keep file I/O bounded and make table/index drops safe on Windows.

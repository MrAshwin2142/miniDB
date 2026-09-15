-- indexing_demo.sql
-- Shows the difference between a full table scan and a B+ tree index seek,
-- using TRACE and EXPLAIN to expose the chosen access path.
-- Run:  minidb --run scripts\indexing_demo.sql  (after setup.sql)
-- or:   EXEC 'scripts/indexing_demo.sql';

USE shop;

-- Turn on the query tracer so every SELECT prints its access path.
TRACE ON;

-- Before any index exists, a filter on 'city' must scan the whole table.
SELECT name FROM customers WHERE city = 'Delhi';

-- EXPLAIN shows the plan without running the query for its result set.
EXPLAIN SELECT name FROM customers WHERE city = 'Delhi';

-- Build a B+ tree index on the city column.
CREATE INDEX idx_city ON customers(city);

-- Now the same query is served by an index seek instead of a full scan.
SELECT name FROM customers WHERE city = 'Delhi';
EXPLAIN SELECT name FROM customers WHERE city = 'Delhi';

-- Index also covers equality on the primary-key-like age column if we add one.
CREATE INDEX idx_age ON customers(age);
EXPLAIN SELECT name FROM customers WHERE age = 30;

-- The index is kept in sync automatically on writes.
INSERT INTO customers VALUES (7, 'Grace', 'Delhi', 30);
SELECT name FROM customers WHERE city = 'Delhi';

TRACE OFF;

-- Inspect indexes and their sizes.
SHOW INDEXES;
STATS;

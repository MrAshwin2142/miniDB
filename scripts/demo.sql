-- demo.sql
-- A guided tour of miniDB. Assumes the 'shop' database from setup.sql exists.
-- Run:  minidb --run scripts\demo.sql   (after running setup.sql once)
-- or:   EXEC 'scripts/demo.sql';

USE shop;

-- Inspect schema and catalog.
DESCRIBE customers;
SHOW CATALOG;

-- Basic projection and filtering.
SELECT id, name, city FROM customers;
SELECT name, age FROM customers WHERE city = 'Delhi';
SELECT name FROM customers WHERE age >= 30 AND city = 'Delhi';

-- Ordering and limiting.
SELECT name, age FROM customers ORDER BY age DESC LIMIT 3;

-- Aggregates.
SELECT COUNT(*) FROM customers;
SELECT MIN(age) FROM customers;
SELECT MAX(age) FROM customers;
SELECT AVG(age) FROM customers;
SELECT SUM(price) FROM products;

-- Mutations.
UPDATE customers SET city = 'Pune' WHERE id = 2;
SELECT id, name, city FROM customers WHERE id = 2;

DELETE FROM customers WHERE id = 6;
SELECT COUNT(*) FROM customers;

-- Engine introspection.
STATS;

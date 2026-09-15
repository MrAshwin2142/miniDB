-- setup.sql
-- Creates a small "shop" database and loads sample data.
-- Run it with:  minidb --run scripts\setup.sql
-- or from inside the shell:  EXEC 'scripts/setup.sql';

CREATE DATABASE shop;
USE shop;

-- Customers: id is the primary key.
CREATE TABLE customers (
    id    INT PRIMARY KEY,
    name  TEXT,
    city  TEXT,
    age   INT
);

INSERT INTO customers VALUES (1, 'Alice',  'Delhi',     30);
INSERT INTO customers VALUES (2, 'Bob',    'Mumbai',    25);
INSERT INTO customers VALUES (3, 'Carol',  'Delhi',     35);
INSERT INTO customers VALUES (4, 'Dave',   'Bengaluru', 40);
INSERT INTO customers VALUES (5, 'Erin',   'Mumbai',    28);
INSERT INTO customers VALUES (6, 'Frank',  'Delhi',     52);

-- Products.
CREATE TABLE products (
    sku    INT PRIMARY KEY,
    title  TEXT,
    price  FLOAT
);

INSERT INTO products VALUES (100, 'Keyboard', 1999.0);
INSERT INTO products VALUES (101, 'Mouse',     899.5);
INSERT INTO products VALUES (102, 'Monitor', 12999.0);
INSERT INTO products VALUES (103, 'USB Cable', 199.0);

SHOW TABLES;

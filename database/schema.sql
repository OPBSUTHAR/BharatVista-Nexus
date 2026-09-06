-- SQLite cache for authorized api.data.gov.in datasets
-- Run: sqlite3 database/osint_cache.db < database/schema.sql

CREATE TABLE IF NOT EXISTS datasets (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    resource_id TEXT NOT NULL,
    sector TEXT NOT NULL CHECK(sector IN ('general','power','agriculture','transport','space','technology')),
    payload TEXT NOT NULL,          -- raw JSON as returned by API (validated)
    fetched_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    limit_val INTEGER,
    offset_val INTEGER
);

CREATE INDEX IF NOT EXISTS idx_datasets_resource ON datasets(resource_id);
CREATE INDEX IF NOT EXISTS idx_datasets_sector ON datasets(sector);
CREATE INDEX IF NOT EXISTS idx_datasets_fetched_at ON datasets(fetched_at);

-- Optional: single-row rate-limit / health helper
CREATE TABLE IF NOT EXISTS fetch_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    resource_id TEXT
);

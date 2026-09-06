# AGENTS.md — Project Instruction & Memory File

> This file is the canonical instruction for any agent / contributor working on this repo. Read it before writing code. It persists across sessions so you don't need to be re-told the goal.

## 1. Goal
Build **BharatVista Nexus** — an enterprise-grade, high-performance **open-data nexus for India & the world in pure C** - backend in C, lightweight web frontend - runnable in VS Code, cross-platform (Linux / macOS / Windows via WSL or MinGW). *Codename: project_C_osint; Public name: BharatVista Nexus.*

**Scope constraint (CRITICAL):**
- Use **only** officially published, freely licensed open data via authorized APIs (e.g. `api.data.gov.in` CKAN API with a registered `DATA_GOV_IN_API_KEY`).
- Respect `robots.txt`, Terms of Service, rate-limits, and publisher licensing.
- **Do NOT** build scrapers for live critical-infrastructure telemetry (SLDC/CEA substation status, real-time SCADA, etc.), do NOT bypass authentication/paywalls, do NOT aggregate sensitive PII.
- For development/demos use **cached or mock samples** or the publisher's sample responses. All network ingestion must be opt-in, key-gated, and cached in SQLite.

## 2. Architecture (do not deviate without updating this file)
```
project_C_osint/
├── src/          # C source: server.c, db.c, data_gov.c, geo.c, parser.c
├── include/      # headers: db.h, data_gov.h, geo.h, parser.h, server.h
├── static/       # index.html, app.js, style.css (Leaflet + OSM)
├── database/     # schema.sql, osint_cache.db (gitignored)
├── .vscode/      # tasks.json, launch.json
├── Makefile      # cross-platform build
├── AGENTS.md     # this file
├── opencode.json # agent/tool config
└── README.md
```

## 3. Backend Contract (C)
- HTTP server: POSIX sockets (WSL/Linux/macOS) + WinSock fallback, `pthreads`, CORS headers.
- Endpoints (stable):
  - `GET /` / `GET /static/*` — static dashboard
  - `GET /api/health` — `{status, uptime, cache_rows}`
  - `GET /api/datasets` — list cached records from SQLite
  - `POST /api/fetch` — body `{resource_id, limit, offset}` → server fetches from `api.data.gov.in/resource/{id}?api-key=...&format=json` via libcurl, caches to SQLite, returns result. Requires `DATA_GOV_IN_API_KEY` env var; rate-limited server-side.
- OSINT modules are **generic dataset viewers**, not sector scrapers: Power, Agriculture, Transport, Space are just tags/filters on top of cached `datasets` table.
- Zero-copy parsing: `jsmn` or custom tokenizer, minimal heap.
- Geospatial kernel: `geo.c` provides bounding-box filter and haversine on cached lat/lng — no bulk polygon ops on sensitive assets.
- SQLite: `database/schema.sql` defines `datasets(id, resource_id, sector, payload, fetched_at)`.

## 4. Frontend Contract
- Single-page `static/index.html` + Leaflet `https://unpkg.com/leaflet` + OpenStreetMap tiles `https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png` (free, no key, attribution required).
- `fetch('/api/health')`, `fetch('/api/datasets')`, `fetch('/api/fetch', {method:'POST'})`.
- No auto-scraping from browser. Map shows generic markers from cached datasets only.

## 5. Build & Run (canonical)
```bash
# env
export DATA_GOV_IN_API_KEY="your_key_from_data.gov.in"
# build
make            # or: make build  (gcc -O2 -std=c11 -pthread -lcurl -lsqlite3)
./build/server  # listens :8080
# or VS Code: Tasks -> Build & Run Server
```
Windows native: use WSL or MinGW-w64 (`mingw32-make`).

## 6. Data Sources (allowlist)
- `api.data.gov.in` — requires key, `format=json`, respect `limit/offset` pagination.
- Any other source MUST be explicitly allowlisted here with its ToS link before adding code.

## 7. Git & Delivery
- Remote: `https://github.com/OPBSUTHAR/project_C_osint` — created via `gh repo create` and pushed (`main` branch).
- GitHub Pages (Step 1): `.github/workflows/pages.yml` deploys `static/` via `actions/deploy-pages@v4` on `push: main`. Enable in GitHub → Settings → Pages → Source: GitHub Actions. Pages is static-only (no C server/SQLite).
- Real-time hosting (Step 2): C server (`build/server`) must run on a container/VM host (Azure/Fly/Render/VPS) with `DATA_GOV_IN_API_KEY` as secret; point Pages frontend to that base URL if needed.
- Never commit `database/*.db`, `.env`, or API keys. Commit `schema.sql` + sample JSON in `database/samples/`.

## 8. Task Tracking
When adding a feature, update this file + `README.md` + `database/schema.sql` if schema changes. Keep `server.c:main` as entry point and document new endpoints here.

---
*Last updated: 2026-09-06 — safe, authorized open-data mode.*

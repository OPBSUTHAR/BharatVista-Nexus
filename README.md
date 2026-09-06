# BharatVista Nexus — Open Data Nexus (Pure C)

*Codename: project_C_osint — Public: BharatVista Nexus*

Enterprise-grade **open-data nexus for India & world** — Pure C backend, Leaflet/OSM + satellite.js frontend. Uses **only** allowlisted public APIs: `api.data.gov.in` (key-gated) + `celestrak.org` (public TLE, attribution).

## Features
- **C backend** `src/server.c` — POSIX sockets + WinSock fallback, `pthreads`, CORS, libcurl, SQLite
- **Endpoints**: `GET /` `GET /satellite.html` `GET /api/health` `GET /api/datasets` `POST /api/fetch` (data.gov.in, key-gated) `GET /api/satellites` `POST /api/satellites/fetch` (Celestrak TLE, 1/60s)
- **Geo kernel** `src/geo.c` — haversine + bbox filter on cached lat/lng; **Satellite** `src/satellite.c` — caches Celestrak TLE to `tle_cache`, client-side SGP4 via `satellite.js`
- **Frontend** `static/index.html` (dashboard) + `static/satellite.html` (real-time ground-track) — Leaflet + OSM (free, attributed) + `satellite.js@5`
- **Cache** `database/schema.sql` (`datasets`, `tle_cache`) + `database/osint_cache.db` (gitignored)

## Quick Start (VS Code)

1. Get an API key from https://data.gov.in → `My Account → API`
2. Set env:
   ```bash
   export DATA_GOV_IN_API_KEY="your_key"  # Windows: $env:DATA_GOV_IN_API_KEY="your_key"
   ```
3. Build & run:
   ```bash
   make
   ./build/server   # http://localhost:8080
   ```
   Windows WSL/MinGW: `mingw32-make` or use VS Code Tasks → *Build & Run Server*
4. Frontend: open `http://localhost:8080` — map markers come from cached `/api/datasets` only.

## Authorized Ingestion Examples
```bash
# data.gov.in (requires DATA_GOV_IN_API_KEY)
curl -X POST http://localhost:8080/api/fetch \
  -H "Content-Type: application/json" \
  -d '{"resource_id":"<uuid-from-data.gov.in-resource-page>","limit":5,"offset":0,"sector":"general"}'
# Server fetches https://api.data.gov.in/resource/<id>?api-key=$DATA_GOV_IN_API_KEY&format=json&limit=5&offset=0

# Celestrak public TLE (no key, cached, rate 1/60s)
curl -X POST http://localhost:8080/api/satellites/fetch -H "Content-Type: application/json" -d '{"group":"stations"}'
curl http://localhost:8080/api/satellites?group=stations
# Open viewer: http://localhost:8080/satellite.html
```

## Project Layout
See `AGENTS.md:2` for architecture. Sector modules (power/agri/transport/space) are **filters on the cached `datasets` table**, not live scrapers.

## Security & Licensing
- Respects `robots.txt`, publisher ToS, rate-limits. Rate-limited server-side (`src/data_gov.c:rate_limit`).
- Never commit `database/*.db`, `.env`, or keys. Sample payloads go in `database/samples/`.

## Hosting

**GitHub Pages (Step 1 — done):**
- `.github/workflows/pages.yml` deploys `static/` (Leaflet + OSM dashboard) to Pages on every `git push` to `main`.
- After push: GitHub → Settings → Pages → Source: **GitHub Actions**. URL is `https://opbsuthar.github.io/BharatVista-Nexus/`.
- Pages is **static-only** — it serves `static/index.html` (BharatVista Nexus viewer) but cannot run the C server (`src/server.c`), `/api/*` or SQLite.

**Real-time C backend (Step 2 — next):**
Host `build/server` where long-running processes are allowed. Example targets:
- Azure Container Apps / App Service (Docker: `gcc -lcurl -lsqlite3`), Fly.io `fly launch`, Render, or a VPS.
- Set `DATA_GOV_IN_API_KEY` as env secret, open port `8080`, then point frontend `fetch('/api/...')` to `https://your-backend/api/...` (update `static/app.js` base URL) or put behind same domain via reverse proxy.
- Keep rate-limit + caching per `AGENTS.md:3` — do NOT move backend to edge/serverless that bypasses SQLite cache.

## Git Remote
Repo: `https://github.com/OPBSUTHAR/BharatVista-Nexus` (renamed from `project_C_osint`) — `main`.

```bash
git remote -v
git push origin main
```

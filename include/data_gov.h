#pragma once

// Fetch from https://api.data.gov.in/resource/{resource_id}?api-key=...&format=json&limit=&offset=
// Requires DATA_GOV_IN_API_KEY env var. Returns malloc'd JSON string (caller free) or NULL.
// Rate-limited: at most 1 request / 2 seconds (server-side).
char* data_gov_fetch(const char *resource_id, int limit, int offset, char *errbuf, size_t errlen);

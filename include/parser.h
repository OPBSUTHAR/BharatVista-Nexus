#pragma once
// Minimal zero-copy JSON helpers (wraps jsmn-like scanning without heap churn)
// For this demo we use small string scans; for production swap to jsmn.

int json_extract_string(const char *json, const char *key, char *out, size_t out_len);

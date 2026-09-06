#pragma once
#include <stddef.h>

// Cached TLE handling (Celestrak public TLE)
// rate-limited server-side 60s per group

char* satellite_fetch_group(const char *group, char *errbuf, size_t errlen); // malloc'd JSON or NULL
char* satellite_list_cached_json(const char *group_filter); // malloc'd JSON, caller free

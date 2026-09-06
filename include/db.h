#pragma once
#include <sqlite3.h>

int db_init(const char *path);
sqlite3* db_handle(void);
void db_close(void);
int db_count_rows(void);
int db_insert_dataset(const char *resource_id, const char *sector, const char *payload, int limit, int offset);
char* db_list_datasets_json(void); // caller free()

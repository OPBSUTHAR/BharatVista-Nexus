#include "db.h"
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static sqlite3 *g_db = NULL;

int db_init(const char *path) {
    int rc = sqlite3_open(path, &g_db);
    if (rc) return rc;
    // Create schema if missing (reads schema.sql at runtime if present)
    const char *ddl =
        "CREATE TABLE IF NOT EXISTS datasets("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " resource_id TEXT NOT NULL,"
        " sector TEXT NOT NULL CHECK(sector IN ('general','power','agriculture','transport','space','technology')),"
        " payload TEXT NOT NULL,"
        " fetched_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),"
        " limit_val INTEGER, offset_val INTEGER);"
        "CREATE INDEX IF NOT EXISTS idx_datasets_resource ON datasets(resource_id);"
        "CREATE INDEX IF NOT EXISTS idx_datasets_sector ON datasets(sector);"
        "CREATE TABLE IF NOT EXISTS tle_cache(id INTEGER PRIMARY KEY AUTOINCREMENT,norad_id TEXT,name TEXT,line1 TEXT,line2 TEXT,grp TEXT,fetched_at TEXT DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')));"
        "CREATE INDEX IF NOT EXISTS idx_tle_grp ON tle_cache(grp);";
    char *err=NULL;
    sqlite3_exec(g_db, ddl, 0,0,&err);
    if(err){ sqlite3_free(err); }
    return 0;
}
sqlite3* db_handle(void){ return g_db; }
void db_close(void){ if(g_db) sqlite3_close(g_db); g_db=NULL; }

int db_count_rows(void){
    sqlite3_stmt *st=NULL;
    int n=0;
    if(sqlite3_prepare_v2(g_db,"SELECT COUNT(*) FROM datasets",-1,&st,NULL)==SQLITE_OK){
        if(sqlite3_step(st)==SQLITE_ROW) n=sqlite3_column_int(st,0);
        sqlite3_finalize(st);
    }
    return n;
}

int db_insert_dataset(const char *resource_id, const char *sector, const char *payload, int limit, int offset){
    sqlite3_stmt *st=NULL;
    const char *sql="INSERT INTO datasets(resource_id,sector,payload,limit_val,offset_val) VALUES(?,?,?,?,?)";
    if(sqlite3_prepare_v2(g_db,sql,-1,&st,NULL)!=SQLITE_OK) return -1;
    sqlite3_bind_text(st,1,resource_id,-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(st,2,sector,-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(st,3,payload,-1,SQLITE_TRANSIENT);
    sqlite3_bind_int(st,4,limit);
    sqlite3_bind_int(st,5,offset);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc==SQLITE_DONE?0:-1;
}

char* db_list_datasets_json(void){
    // Returns {"count":N,"rows":[{id,resource_id,sector,fetched_at,payload},{...}]}
    sqlite3_stmt *st=NULL;
    if(sqlite3_prepare_v2(g_db,"SELECT id,resource_id,sector,fetched_at,payload FROM datasets ORDER BY fetched_at DESC LIMIT 100",-1,&st,NULL)!=SQLITE_OK) return NULL;
    size_t cap=65536; size_t len=0; char *buf=(char*)malloc(cap); if(!buf) return NULL;
    len += snprintf(buf+len, cap-len, "{\"count\":%d,\"rows\":[", db_count_rows());
    bool first=true;
    while(sqlite3_step(st)==SQLITE_ROW){
        int id=sqlite3_column_int(st,0);
        const unsigned char *rid=sqlite3_column_text(st,1);
        const unsigned char *sector=sqlite3_column_text(st,2);
        const unsigned char *fetched=sqlite3_column_text(st,3);
        const unsigned char *payload=sqlite3_column_text(st,4);
        // payload is JSON - embed as object if looks like object, else string
        const char *pay = (const char*)payload;
        // ensure space
        size_t need = 256 + (pay?strlen(pay):0);
        if(len+need >= cap){ cap=(cap+need)*2; char *nb=(char*)realloc(buf,cap); if(!nb){free(buf); sqlite3_finalize(st); return NULL;} buf=nb; }
        if(!first) buf[len++]=',';
        first=false;
        // naive escape for rid/sector/fetched (alphanumeric)
        len += snprintf(buf+len, cap-len, "{\"id\":%d,\"resource_id\":\"%s\",\"sector\":\"%s\",\"fetched_at\":\"%s\",\"payload\":", id, rid? (const char*)rid:"", sector? (const char*)sector:"", fetched? (const char*)fetched:"");
        if(pay && pay[0]=='{' && pay[strlen(pay)-1]=='}') len += snprintf(buf+len, cap-len, "%s}", pay);
        else if(pay) len += snprintf(buf+len, cap-len, "\"%s\"}", pay);
        else len += snprintf(buf+len, cap-len, "null}");
    }
    sqlite3_finalize(st);
    if(len+4>=cap){ cap+=16; char*nb=(char*)realloc(buf,cap); if(!nb){free(buf); return NULL;} buf=nb; }
    snprintf(buf+len, cap-len, "]}");
    return buf;
}

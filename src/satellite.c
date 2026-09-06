#include "satellite.h"
#include "db.h"
#include <curl/curl.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>

static time_t g_last_fetch = 0;

struct buf { char *data; size_t len; size_t cap; };
static size_t write_cb(void *c, size_t s, size_t n, void *u){
    size_t rs=s*n;
    struct buf *b=(struct buf*)u;
    if(b->len+rs+1 > b->cap){
        size_t nc=(b->cap==0?32768:b->cap*2)+rs;
        char *nd=(char*)realloc(b->data,nc);
        if(!nd) return 0;
        b->data=nd; b->cap=nc;
    }
    memcpy(b->data+b->len,c,rs);
    b->len+=rs; b->data[b->len]='\0';
    return rs;
}

static int ensure_table(){
    sqlite3 *db=db_handle();
    const char *ddl="CREATE TABLE IF NOT EXISTS tle_cache("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "norad_id TEXT, name TEXT, line1 TEXT, line2 TEXT, grp TEXT,"
        "fetched_at TEXT DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')));"
        "CREATE INDEX IF NOT EXISTS idx_tle_grp ON tle_cache(grp);"
        "CREATE INDEX IF NOT EXISTS idx_tle_norad ON tle_cache(norad_id);";
    char *err=NULL;
    int rc=sqlite3_exec(db,ddl,0,0,&err);
    if(err) sqlite3_free(err);
    return rc;
}

// TLE is 3-line: name, line1, line2 repeated. Convert to JSON array.
static char* tle_to_json(const char *tle_text, const char *group){
    ensure_table();
    sqlite3 *db=db_handle();
    // Clear previous cache for this group
    sqlite3_stmt *del=NULL;
    sqlite3_prepare_v2(db,"DELETE FROM tle_cache WHERE grp=?",-1,&del,NULL);
    sqlite3_bind_text(del,1,group,-1,SQLITE_TRANSIENT);
    sqlite3_step(del); sqlite3_finalize(del);

    // Parse lines
    size_t count=0;
    char *copy=strdup(tle_text);
    char *line=strtok(copy,"\r\n");
    char *name=NULL,*l1=NULL,*l2=NULL;
    // Build JSON array incrementally
    size_t cap=65536, len=0; char *json=(char*)malloc(cap);
    len+=snprintf(json+len,cap-len,"{\"group\":\"%s\",\"fetched_at\":\"%s\",\"count\":",group, "now");
    // placeholder for count, will patch later
    size_t count_pos=len;
    len+=snprintf(json+len,cap-len,"0,\"tles\":[");
    bool first=true;
    sqlite3_stmt *ins=NULL;
    sqlite3_prepare_v2(db,"INSERT INTO tle_cache(norad_id,name,line1,line2,grp) VALUES(?,?,?,?,?)",-1,&ins,NULL);

    while(line){
        // trim
        while(*line && isspace((unsigned char)*line)) line++;
        if(*line){
            if(!name) name=line;
            else if(!l1) l1=line;
            else if(!l2){ l2=line;
                // got triple
                // extract norad_id from line1 (columns 3-7)
                char norad[16]={0};
                if(strlen(l1)>=7) { memcpy(norad,l1+2,5); norad[5]='\0'; }
                // insert
                sqlite3_bind_text(ins,1,norad,-1,SQLITE_TRANSIENT);
                sqlite3_bind_text(ins,2,name,-1,SQLITE_TRANSIENT);
                sqlite3_bind_text(ins,3,l1,-1,SQLITE_TRANSIENT);
                sqlite3_bind_text(ins,4,l2,-1,SQLITE_TRANSIENT);
                sqlite3_bind_text(ins,5,group,-1,SQLITE_TRANSIENT);
                sqlite3_step(ins); sqlite3_reset(ins);
                // append json
                size_t need=strlen(name)+strlen(l1)+strlen(l2)+128;
                if(len+need>=cap){ cap=(cap+need)*2; char *nb=(char*)realloc(json,cap); if(!nb) break; json=nb; }
                if(!first) json[len++]=',';
                first=false;
                // escape name quotes
                char esc_name[256]; size_t k=0;
                for(size_t i=0;i<strlen(name)&&k<200;i++){ if(name[i]=='"'||name[i]=='\\') esc_name[k++]='\\'; esc_name[k++]=name[i]; } esc_name[k]='\0';
                len+=snprintf(json+len,cap-len,"{\"name\":\"%s\",\"norad_id\":\"%s\",\"line1\":\"%s\",\"line2\":\"%s\"}", esc_name,norad,l1,l2);
                count++;
                name=l1=l2=NULL;
            }
        }
        line=strtok(NULL,"\r\n");
    }
    free(copy);
    sqlite3_finalize(ins);
    if(len+4>=cap){ cap+=32; json=(char*)realloc(json,cap); }
    snprintf(json+len,cap-len,"]}");
    // patch count
    char tmp[32]; snprintf(tmp,sizeof(tmp),"%zu",(size_t)count);
    // naive: replace "count":0 with actual (we reserved one char "0" — if count>9, JSON grows)
    // Rebuild header if needed
    if(count>9){
        // shift
        size_t header_len=strlen(json);
        // find "\"count\":" position near start
        char *p=strstr(json,"\"count\":");
        if(p){
            // regenerate small buffer: easier to rebuild prefix
            // leave as is: count field already 0 may be wrong for >9, fix by rewriting prefix slice
            // quick fix: memmove tail
            size_t prefix=p-json;
            size_t tail_len=strlen(p+9); // after "0,\"tles"
            // Actually structure: "count":0,"tles":[  => after count we have ,"tles"
            // Move tail forward if digits >1
            size_t digits=strlen(tmp);
            if(digits!=1){
                memmove(p+8+digits, p+9, tail_len+1);
                memcpy(p+8,tmp,digits);
            }
        }
    } else {
        json[count_pos]= '0'+ (char)count;
    }
    return json;
}

char* satellite_fetch_group(const char *group, char *errbuf, size_t errlen){
    if(!group || !group[0]) { snprintf(errbuf,errlen,"missing group (e.g. stations, active, gps-ops, weather)"); return NULL; }
    // sanitize group: alnum, '-' only
    for(const char *p=group;*p;p++){ if(!isalnum((unsigned char)*p) && *p!='-' && *p!='_' ){ snprintf(errbuf,errlen,"invalid group"); return NULL; } }
    time_t now=time(NULL);
    if(g_last_fetch && difftime(now,g_last_fetch)<60){
        snprintf(errbuf,errlen,"rate_limited: retry after %d sec",(int)(60-difftime(now,g_last_fetch)));
        return NULL;
    }
    char url[512];
    snprintf(url,sizeof(url),"https://celestrak.org/NORAD/elements/gp.php?GROUP=%s&FORMAT=tle",group);
    CURL *curl=curl_easy_init();
    if(!curl){ snprintf(errbuf,errlen,"curl init failed"); return NULL; }
    struct buf b={0};
    curl_easy_setopt(curl,CURLOPT_URL,url);
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT,20L);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,write_cb);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&b);
    curl_easy_setopt(curl,CURLOPT_USERAGENT,"BharatVista-Nexus/1.0 (+https://github.com/OPBSUTHAR/BharatVista-Nexus)");
    CURLcode rc=curl_easy_perform(curl);
    long code=0; curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&code);
    curl_easy_cleanup(curl);
    if(rc!=CURLE_OK){ snprintf(errbuf,errlen,"curl %s",curl_easy_strerror(rc)); free(b.data); return NULL; }
    if(code!=200){ snprintf(errbuf,errlen,"celestrak HTTP %ld %.200s",code,b.data?b.data:""); free(b.data); return NULL; }
    if(!b.data || strlen(b.data)<50){ snprintf(errbuf,errlen,"empty TLE response"); free(b.data); return NULL; }
    g_last_fetch=now;
    char* json=tle_to_json(b.data,group);
    free(b.data);
    return json;
}

char* satellite_list_cached_json(const char *group_filter){
    ensure_table();
    sqlite3 *db=db_handle();
    sqlite3_stmt *st=NULL;
    const char *sql="SELECT norad_id,name,line1,line2,grp,fetched_at FROM tle_cache WHERE (? IS NULL OR grp=?) ORDER BY grp, norad_id LIMIT 500";
    sqlite3_prepare_v2(db,sql,-1,&st,NULL);
    if(group_filter && group_filter[0]){
        sqlite3_bind_text(st,1,group_filter,-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(st,2,group_filter,-1,SQLITE_TRANSIENT);
    } else { sqlite3_bind_null(st,1); sqlite3_bind_null(st,2); }
    size_t cap=65536,len=0; char *out=(char*)malloc(cap); if(!out) return NULL;
    len+=snprintf(out+len,cap-len,"{\"tles\":[");
    bool first=true;
    char last_grp[64]="";
    // we will also return distinct groups count
    while(sqlite3_step(st)==SQLITE_ROW){
        const unsigned char *norad=sqlite3_column_text(st,0);
        const unsigned char *name=sqlite3_column_text(st,1);
        const unsigned char *l1=sqlite3_column_text(st,2);
        const unsigned char *l2=sqlite3_column_text(st,3);
        // grp at col 4
        size_t need=512 + (name?strlen((char*)name):0);
        if(len+need>=cap){ cap=(cap+need)*2; char *nb=(char*)realloc(out,cap); if(!nb){free(out); sqlite3_finalize(st); return NULL;} out=nb; }
        if(!first) out[len++]=',';
        first=false;
        char esc[256]; size_t k=0; const char *n=(char*)name;
        for(size_t i=0;n && n[i] && k<200;i++){ if(n[i]=='"'||n[i]=='\\') esc[k++]='\\'; esc[k++]=n[i]; } esc[k]='\0';
        len+=snprintf(out+len,cap-len,"{\"norad_id\":\"%s\",\"name\":\"%s\",\"line1\":\"%s\",\"line2\":\"%s\",\"group\":\"%s\"}",
            norad? (char*)norad:"", esc, l1? (char*)l1:"", l2? (char*)l2:"", sqlite3_column_text(st,4)? (char*)sqlite3_column_text(st,4):"");
    }
    sqlite3_finalize(st);
    snprintf(out+len,cap-len,"]}");
    return out;
}

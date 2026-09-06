#include "data_gov.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

struct buf { char *data; size_t len; size_t cap; };

static size_t write_cb(void *c, size_t s, size_t n, void *u){
    size_t rs = s*n;
    struct buf *b=(struct buf*)u;
    if(b->len+rs+1 > b->cap){
        size_t nc = (b->cap==0?8192:b->cap*2)+rs;
        char *nd=(char*)realloc(b->data,nc);
        if(!nd) return 0;
        b->data=nd; b->cap=nc;
    }
    memcpy(b->data+b->len,c,rs);
    b->len+=rs; b->data[b->len]='\0';
    return rs;
}

// Simple process-wide rate limit: 1 req / 2 sec
static time_t g_last=0;
static int rate_ok(char *errbuf,size_t errlen){
    time_t now=time(NULL);
    if(g_last!=0 && difftime(now,g_last) < 2.0){
        snprintf(errbuf,errlen,"rate_limited: retry after %d sec",(int)(2-difftime(now,g_last)));
        return 0;
    }
    g_last=now;
    return 1;
}

char* data_gov_fetch(const char *resource_id, int limit, int offset, char *errbuf, size_t errlen){
    if(!rate_ok(errbuf,errlen)) return NULL;
    const char *key = getenv("DATA_GOV_IN_API_KEY");
    if(!key || !key[0]){
        snprintf(errbuf,errlen,"missing DATA_GOV_IN_API_KEY env var (register at data.gov.in)");
        return NULL;
    }
    if(!resource_id || strlen(resource_id)<8){
        snprintf(errbuf,errlen,"invalid resource_id (UUID from data.gov.in resource page)");
        return NULL;
    }
    if(limit<=0) limit=5;
    if(limit>100) limit=100;
    if(offset<0) offset=0;

    char url[1024];
    snprintf(url,sizeof(url),"https://api.data.gov.in/resource/%s?api-key=%s&format=json&limit=%d&offset=%d",
        resource_id, key, limit, offset);

    CURL *curl=curl_easy_init();
    if(!curl){ snprintf(errbuf,errlen,"curl init failed"); return NULL; }
    struct buf b={0};
    curl_easy_setopt(curl,CURLOPT_URL,url);
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT,15L);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,write_cb);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&b);
    curl_easy_setopt(curl,CURLOPT_USERAGENT,"project_C_osint/1.0 (+https://data.gov.in)");
    CURLcode rc=curl_easy_perform(curl);
    long code=0; curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&code);
    curl_easy_cleanup(curl);
    if(rc!=CURLE_OK){
        snprintf(errbuf,errlen,"curl error: %s", curl_easy_strerror(rc));
        free(b.data); return NULL;
    }
    if(code!=200){
        snprintf(errbuf,errlen,"data.gov.in HTTP %ld: %.*s", code, (int)(b.len<300?b.len:300), b.data?b.data:"");
        free(b.data); return NULL;
    }
    return b.data; // caller free() and caches to SQLite
}

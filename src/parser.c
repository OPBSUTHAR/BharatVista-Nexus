#include "parser.h"
#include <string.h>
#include <stdlib.h>

// Extremely small zero-copy style helper: finds "key":"value" (string)
int json_extract_string(const char *json, const char *key, char *out, size_t out_len){
    if(!json||!key||!out) return -1;
    char pat[128];
    snprintf(pat,sizeof(pat),"\"%s\"",key);
    const char *p=strstr(json,pat);
    if(!p) return -1;
    p=strchr(p,':'); if(!p) return -1; p++;
    while(*p==' '||*p=='\t'||*p=='\n'||*p=='\r') p++;
    if(*p!='"') return -1; p++;
    size_t i=0;
    while(*p && *p!='"' && i+1<out_len){ out[i++]=*p++; }
    out[i]='\0';
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib,"ws2_32.lib")
  typedef SOCKET sock_t;
  #define CLOSE_SOCK closesocket
  #define SOCK_ERR SOCKET_ERROR
#else
  #include <unistd.h>
  #include <arpa/inet.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  typedef int sock_t;
  #define CLOSE_SOCK close
  #define SOCK_ERR -1
  #define INVALID_SOCKET -1
#endif
#include <pthread.h>
#include <sqlite3.h>
#include <curl/curl.h>

#include "db.h"
#include "data_gov.h"
#include "parser.h"

static time_t g_started;
static const char *DB_PATH = "database/osint_cache.db";

// ---------- tiny HTTP helpers ----------
static void send_resp(sock_t c, int code, const char *ctype, const char *body, size_t blen){
    const char *msg = code==200?"OK":code==404?"Not Found":code==429?"Too Many Requests":code==400?"Bad Request":"Error";
    char head[1024];
    int hlen = snprintf(head,sizeof(head),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n\r\n", code,msg,ctype,blen);
    send(c,head,hlen,0);
    if(blen) send(c,body,(int)blen,0);
}

static char* read_file(const char *path, size_t *out_len, const char **ctype){
    const char *ct="text/plain";
    if(strstr(path,".html")) ct="text/html";
    else if(strstr(path,".js")) ct="application/javascript";
    else if(strstr(path,".css")) ct="text/css";
    else if(strstr(path,".json")) ct="application/json";
    if(ctype) *ctype=ct;
    FILE *f=fopen(path,"rb");
    if(!f) return NULL;
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char *b=(char*)malloc(sz+1); if(!b){fclose(f); return NULL;}
    fread(b,1,sz,f); b[sz]='\0'; fclose(f);
    if(out_len) *out_len=sz;
    return b;
}

// ---------- routing ----------
static void handle_client(sock_t c){
    char buf[8192]; int n=recv(c,buf,sizeof(buf)-1,0);
    if(n<=0){ CLOSE_SOCK(c); return; }
    buf[n]='\0';

    char method[16]={0}, path[512]={0};
    sscanf(buf,"%15s %511s",method,path);

    // CORS preflight
    if(strcmp(method,"OPTIONS")==0){
        send_resp(c,200,"text/plain","",0); CLOSE_SOCK(c); return;
    }

    if(strcmp(method,"GET")==0 && (strcmp(path,"/")==0 || strcmp(path,"/index.html")==0)){
        size_t len; const char *ct; char *b=read_file("static/index.html",&len,&ct);
        if(!b){ send_resp(c,404,"text/plain","not found",9); }
        else { send_resp(c,200,ct,b,len); free(b); }
        CLOSE_SOCK(c); return;
    }
    if(strncmp(path,"/static/",8)==0){
        char fpath[600]; snprintf(fpath,sizeof(fpath),".%s",path);
        size_t len; const char *ct; char *b=read_file(fpath,&len,&ct);
        if(!b) send_resp(c,404,"text/plain","not found",9);
        else { send_resp(c,200,ct,b,len); free(b); }
        CLOSE_SOCK(c); return;
    }
    if(strcmp(method,"GET")==0 && strcmp(path,"/api/health")==0){
        int rows=db_count_rows();
        long up=(long)difftime(time(NULL),g_started);
        char body[256]; int blen=snprintf(body,sizeof(body),"{\"status\":\"ok\",\"uptime\":%ld,\"cache_rows\":%d}",up,rows);
        send_resp(c,200,"application/json",body,blen);
        CLOSE_SOCK(c); return;
    }
    if(strcmp(method,"GET")==0 && strncmp(path,"/api/datasets",13)==0){
        char *j=db_list_datasets_json();
        if(!j) j=strdup("{\"count\":0,\"rows\":[]}");
        send_resp(c,200,"application/json",j,strlen(j));
        free(j); CLOSE_SOCK(c); return;
    }
    if(strcmp(method,"POST")==0 && strncmp(path,"/api/fetch",10)==0){
        // extract body
        char *body=strstr(buf,"\r\n\r\n");
        if(body) body+=4; else body="";
        char resource_id[128]={0}, sector[32]="general";
        int limit=5, offset=0;
        // very small JSON parse: look for resource_id, limit, offset, sector
        json_extract_string(body,"resource_id",resource_id,sizeof(resource_id));
        char tmp[32];
        if(json_extract_string(body,"sector",tmp,sizeof(tmp))==0) strncpy(sector,tmp,sizeof(sector)-1);
        // normalize sector
        if(strcmp(sector,"power")&&strcmp(sector,"agriculture")&&strcmp(sector,"transport")&&strcmp(sector,"space")&&strcmp(sector,"technology")) strcpy(sector,"general");
        // numeric fields
        const char *p=strstr(body,"\"limit\""); if(p){ sscanf(p,"\"limit\"%*[: ]%d",&limit); }
        p=strstr(body,"\"offset\""); if(p){ sscanf(p,"\"offset\"%*[: ]%d",&offset); }

        char err[512]={0};
        char *payload=data_gov_fetch(resource_id[0]?resource_id:NULL,limit,offset,err,sizeof(err));
        if(!payload){
            char ebody[768]; int blen=snprintf(ebody,sizeof(ebody),"{\"error\":\"%s\"}",err[0]?err:"fetch failed");
            int code=strstr(err,"rate_limited")?429:400;
            send_resp(c,code,"application/json",ebody,blen);
            CLOSE_SOCK(c); return;
        }
        db_insert_dataset(resource_id,sector,payload,limit,offset);
        // return payload as-is but wrapped
        send_resp(c,200,"application/json",payload,strlen(payload));
        free(payload); CLOSE_SOCK(c); return;
    }
    send_resp(c,404,"application/json","{\"error\":\"not found\"}",21);
    CLOSE_SOCK(c);
}

static void* client_thread(void *arg){
    sock_t c=(sock_t)(intptr_t)arg;
    handle_client(c);
    return NULL;
}

int main(int argc, char **argv){
    const char *port = argc>1?argv[1]:"8080";
    g_started=time(NULL);

#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2),&wsa);
#endif
    curl_global_init(CURL_GLOBAL_ALL);
    if(db_init(DB_PATH)!=0){ fprintf(stderr,"db open failed: %s\n",DB_PATH); return 1; }

    int p = atoi(port);
    sock_t srv=socket(AF_INET,SOCK_STREAM,0);
    if(srv==INVALID_SOCKET){ perror("socket"); return 1; }
    int opt=1;
#ifndef _WIN32
    setsockopt(srv,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
#endif
    struct sockaddr_in addr={0};
    addr.sin_family=AF_INET; addr.sin_addr.s_addr=INADDR_ANY; addr.sin_port=htons(p);
    if(bind(srv,(struct sockaddr*)&addr,sizeof(addr))==SOCK_ERR){ perror("bind"); return 1; }
    if(listen(srv,32)==SOCK_ERR){ perror("listen"); return 1; }
    printf("[+] C open-data server listening on :%d (DB=%s)\n",p,DB_PATH);
    printf("[+] GET /  GET /api/health  GET /api/datasets  POST /api/fetch\n");
    printf("[+] Set DATA_GOV_IN_API_KEY env var before POST /api/fetch\n");

    while(1){
        sock_t c=accept(srv,NULL,NULL);
        if(c==INVALID_SOCKET) continue;
        pthread_t th; pthread_create(&th,NULL,client_thread,(void*)(intptr_t)c); pthread_detach(th);
    }
    // unreachable
    db_close(); curl_global_cleanup();
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

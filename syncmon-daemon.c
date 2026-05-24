#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <libgen.h>

#define CONFIG_FILE_DEFAULT   "/etc/syncmon.d/config.conf"
#define STATE_FILE_DEFAULT    "/var/log/syncmon/syncmon_state.env"
#define LOG_FILE_DEFAULT      "/var/log/syncmon/syncmon.log"
#define TMP_STATE_SUFFIX      ".tmp"
#define MAX_BUFFER            2048

typedef struct {
    int check_interval;
    int enable_mysql_check;
    int enable_redis_check;
    int log_max_size;
    int startup_check;
    int exit_on_startup_failure;
    char log_file[PATH_MAX];
    char state_file[PATH_MAX];

    char mysql_master_host[256];
    char mysql_master_port[16];
    char mysql_slave_host[256];
    char mysql_slave_port[16];
    char mysql_user[256];
    char mysql_password[256];

    char redis_master_host[256];
    char redis_master_port[16];
    char redis_slave_host[256];
    char redis_slave_port[16];
    char redis_password[256];

    /* placeholder component addresses */
    char lb_host[256];
    char dns_host[256];
    char nc1_host[256];
    char nc2_host[256];
    char nfs_host[256];
    /* optional URL overrides for HTTP checks */
    char lb_check_url[512];
    char nc1_check_url[512];
    char nc2_check_url[512];
} Config;

void init_config(Config *cfg) {
    cfg->check_interval          = 30;
    cfg->enable_mysql_check      = 1;
    cfg->enable_redis_check      = 1;
    cfg->log_max_size            = 100;
    cfg->startup_check           = 1;
    cfg->exit_on_startup_failure = 0;
    snprintf(cfg->log_file,   PATH_MAX, "%s", LOG_FILE_DEFAULT);
    snprintf(cfg->state_file, PATH_MAX, "%s", STATE_FILE_DEFAULT);
    snprintf(cfg->mysql_master_host, sizeof(cfg->mysql_master_host), "127.0.0.1");
    snprintf(cfg->mysql_master_port, sizeof(cfg->mysql_master_port), "3306");
    snprintf(cfg->mysql_slave_host,  sizeof(cfg->mysql_slave_host),  "127.0.0.2");
    snprintf(cfg->mysql_slave_port,  sizeof(cfg->mysql_slave_port),  "3306");
    snprintf(cfg->redis_master_host, sizeof(cfg->redis_master_host), "127.0.0.1");
    snprintf(cfg->redis_master_port, sizeof(cfg->redis_master_port), "6379");
    snprintf(cfg->redis_slave_host,  sizeof(cfg->redis_slave_host),  "127.0.0.2");
    snprintf(cfg->redis_slave_port,  sizeof(cfg->redis_slave_port),  "6379");
    snprintf(cfg->lb_host,  sizeof(cfg->lb_host),  "not configured");
    snprintf(cfg->dns_host, sizeof(cfg->dns_host), "not configured");
    snprintf(cfg->nc1_host, sizeof(cfg->nc1_host), "not configured");
    snprintf(cfg->nc2_host, sizeof(cfg->nc2_host), "not configured");
    snprintf(cfg->nfs_host, sizeof(cfg->nfs_host), "not configured");
    cfg->lb_check_url[0]  = '\0';
    cfg->nc1_check_url[0] = '\0';
    cfg->nc2_check_url[0] = '\0';
}

void trim_newline(char *str) { str[strcspn(str, "\r\n")] = 0; }

void ensure_log_dir(const char *path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *slash = strrchr(tmp, '/');
    if (slash) {
        *slash = '\0';
        for (char *p = tmp+1; *p; p++) {
            if (*p=='/') { *p='\0'; mkdir(tmp,0755); *p='/'; }
        }
        mkdir(tmp, 0755);
    }
}

void load_config(const char *filepath, Config *cfg) {
    FILE *f = fopen(filepath, "r");
    if (!f) { fprintf(stderr, "ERROR: configuration file not found: %s\n", filepath); exit(1); }
    char line[MAX_BUFFER];
    while (fgets(line, sizeof(line), f)) {
        char *p = strchr(line, '#'); if (p) *p = 0;
        trim_newline(line);
        if (strlen(line) == 0) continue;
        char key[256]={0}, val[256]={0};
        if (sscanf(line, "%255[^=]=%255[^\n]", key, val) == 2) {
            if ((val[0]=='"'||val[0]=='\'')
                && val[strlen(val)-1]==val[0]) {
                val[strlen(val)-1]='\0'; memmove(val,val+1,strlen(val));
            }
            if      (strcmp(key,"CHECK_INTERVAL")==0)          cfg->check_interval=atoi(val);
            else if (strcmp(key,"ENABLE_MYSQL_CHECK")==0)       cfg->enable_mysql_check=atoi(val);
            else if (strcmp(key,"ENABLE_REDIS_CHECK")==0)       cfg->enable_redis_check=atoi(val);
            else if (strcmp(key,"LOG_MAX_SIZE")==0)             cfg->log_max_size=atoi(val);
            else if (strcmp(key,"STARTUP_CHECK")==0)            cfg->startup_check=atoi(val);
            else if (strcmp(key,"EXIT_ON_STARTUP_FAILURE")==0)  cfg->exit_on_startup_failure=atoi(val);
            else if (strcmp(key,"LOG_FILE")==0)                 snprintf(cfg->log_file,sizeof(cfg->log_file),"%s",val);
            else if (strcmp(key,"STATE_FILE")==0)               snprintf(cfg->state_file,sizeof(cfg->state_file),"%s",val);
            else if (strcmp(key,"MYSQL_MASTER_HOST")==0)        snprintf(cfg->mysql_master_host,sizeof(cfg->mysql_master_host),"%s",val);
            else if (strcmp(key,"MYSQL_MASTER_PORT")==0)        snprintf(cfg->mysql_master_port,sizeof(cfg->mysql_master_port),"%s",val);
            else if (strcmp(key,"MYSQL_SLAVE_HOST")==0)         snprintf(cfg->mysql_slave_host,sizeof(cfg->mysql_slave_host),"%s",val);
            else if (strcmp(key,"MYSQL_SLAVE_PORT")==0)         snprintf(cfg->mysql_slave_port,sizeof(cfg->mysql_slave_port),"%s",val);
            else if (strcmp(key,"MYSQL_USER")==0)               snprintf(cfg->mysql_user,sizeof(cfg->mysql_user),"%s",val);
            else if (strcmp(key,"MYSQL_PASSWORD")==0)           snprintf(cfg->mysql_password,sizeof(cfg->mysql_password),"%s",val);
            else if (strcmp(key,"REDIS_MASTER_HOST")==0)        snprintf(cfg->redis_master_host,sizeof(cfg->redis_master_host),"%s",val);
            else if (strcmp(key,"REDIS_MASTER_PORT")==0)        snprintf(cfg->redis_master_port,sizeof(cfg->redis_master_port),"%s",val);
            else if (strcmp(key,"REDIS_SLAVE_HOST")==0)         snprintf(cfg->redis_slave_host,sizeof(cfg->redis_slave_host),"%s",val);
            else if (strcmp(key,"REDIS_SLAVE_PORT")==0)         snprintf(cfg->redis_slave_port,sizeof(cfg->redis_slave_port),"%s",val);
            else if (strcmp(key,"REDIS_PASSWORD")==0)           snprintf(cfg->redis_password,sizeof(cfg->redis_password),"%s",val);
            else if (strcmp(key,"LB_HOST")==0)                  snprintf(cfg->lb_host,sizeof(cfg->lb_host),"%s",val);
            else if (strcmp(key,"DNS_HOST")==0)                 snprintf(cfg->dns_host,sizeof(cfg->dns_host),"%s",val);
            else if (strcmp(key,"NC1_HOST")==0)                 snprintf(cfg->nc1_host,sizeof(cfg->nc1_host),"%s",val);
            else if (strcmp(key,"NC2_HOST")==0)                 snprintf(cfg->nc2_host,sizeof(cfg->nc2_host),"%s",val);
            else if (strcmp(key,"NFS_HOST")==0)                 snprintf(cfg->nfs_host,sizeof(cfg->nfs_host),"%s",val);
            else if (strcmp(key,"LB_CHECK_URL")==0)             snprintf(cfg->lb_check_url,sizeof(cfg->lb_check_url),"%s",val);
            else if (strcmp(key,"NC1_CHECK_URL")==0)            snprintf(cfg->nc1_check_url,sizeof(cfg->nc1_check_url),"%s",val);
            else if (strcmp(key,"NC2_CHECK_URL")==0)            snprintf(cfg->nc2_check_url,sizeof(cfg->nc2_check_url),"%s",val);
        }
    }
    fclose(f);
}

void log_message(Config *cfg, const char *msg) {
    if (cfg->log_max_size > 0) {
        struct stat st;
        if (stat(cfg->log_file,&st)==0 && st.st_size>(cfg->log_max_size*1024*1024)) {
            char old[PATH_MAX+8]; snprintf(old,sizeof(old),"%s.old",cfg->log_file);
            rename(cfg->log_file, old);
        }
    }
    FILE *f = fopen(cfg->log_file, "a");
    if (f) {
        time_t t=time(NULL); struct tm tm=*localtime(&t);
        fprintf(f,"%04d-%02d-%02d %02d:%02d:%02d %s\n",
                tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,
                tm.tm_hour,tm.tm_min,tm.tm_sec,msg);
        fclose(f);
    }
}

/* ── ping_host: single ICMP ping, returns latency string and status ──────── */
void ping_host(const char *host, char *status_out, char *detail_out) {
    char cmd[512];
    /* -c1 -W2: one packet, 2 s timeout; parse avg rtt from summary line */
    snprintf(cmd, sizeof(cmd),
             "ping -c1 -W2 %s 2>/dev/null"
             " | awk '/rtt|round-trip/{split($4,a,\"/\"); printf \"%.0fms\",a[2]}'\n",
             host);
    FILE *fp = popen(cmd, "r");
    char buf[64] = {0};
    if (fp) { fgets(buf, sizeof(buf), fp); pclose(fp); }
    buf[strcspn(buf, "\r\n")] = 0;
    if (strlen(buf) == 0) {
        /* no rtt output → unreachable */
        snprintf(status_out, 16,  "ERROR");
        snprintf(detail_out, 64,  "timeout");
    } else {
        int ms = atoi(buf);
        snprintf(detail_out, 64, "%s", buf);
        if      (ms < 100)  snprintf(status_out, 16, "OK");
        else if (ms < 500)  snprintf(status_out, 16, "WARN");
        else                snprintf(status_out, 16, "ERROR");
    }
}

/* ── http_check: curl GET, returns HTTP status code + body snippet ──────── */
void http_check(const char *url, char *result_out, size_t
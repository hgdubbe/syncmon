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

/* ── now_str: fill buf with current timestamp ─────────────────────────────────────── */
static void now_str(char *buf, size_t sz) {
    time_t t=time(NULL); struct tm tm=*localtime(&t);
    snprintf(buf,sz,"%04d-%02d-%02d %02d:%02d:%02d",
             tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,
             tm.tm_hour,tm.tm_min,tm.tm_sec);
}

/* ── ping_host: ICMP ping, fills status ("OK"/"WARN"/"ERROR") + latency ─── */
void ping_host(const char *host, char *status_out, char *detail_out) {
    /*
     * Build the command in two parts to prevent GCC from scanning the
     * awk printf format string ("%.0fms") as a C format specifier.
     * The awk snippet is a shell string, not a C printf argument.
     */
    char cmd[512];
    const char *awk_frag = " | awk '/rtt|round-trip/{split($4,a,\"/\"); printf \"%.0fms\",a[2]}'";
    snprintf(cmd, sizeof(cmd), "ping -c1 -W2 %s 2>/dev/null%s", host, awk_frag);
    FILE *fp = popen(cmd, "r");
    char buf[64] = {0};
    if (fp) { (void)fgets(buf, sizeof(buf), fp); pclose(fp); }
    buf[strcspn(buf, "\r\n")] = 0;
    if (strlen(buf) == 0) {
        snprintf(status_out, 16, "ERROR");
        snprintf(detail_out, 64, "timeout");
    } else {
        int ms = atoi(buf);
        snprintf(detail_out, 64, "%s", buf);
        if      (ms < 100) snprintf(status_out, 16, "OK");
        else if (ms < 500) snprintf(status_out, 16, "WARN");
        else               snprintf(status_out, 16, "ERROR");
    }
}

/* ── http_check: curl GET, fills "HTTP <code> <first-meaningful-field>" ─── */
void http_check(const char *url, char *result_out, size_t result_sz) {
    char cmd[768];
    /* -s silent, -o /dev/null, -w print code; then grab first 120 chars of body */
    snprintf(cmd, sizeof(cmd),
             "CODE=$(curl -sk -o /tmp/syncmon_http_body -w '%%{http_code}' "
             "--max-time 5 '%s' 2>/dev/null); "
             "BODY=$(head -c 120 /tmp/syncmon_http_body 2>/dev/null | tr -d '\\n'); "
             "echo \"HTTP $CODE $BODY\"",
             url);
    FILE *fp = popen(cmd, "r");
    if (!fp) { snprintf(result_out, result_sz, "curl error"); return; }
    char buf[256] = {0};
    (void)fgets(buf, sizeof(buf), fp);
    pclose(fp);
    buf[strcspn(buf, "\r\n")] = 0;
    snprintf(result_out, result_sz, "%s", buf);
}

/* ── dns_check: dig lookup, fills "resolv OK: <addr>" or error ───────────── */
void dns_check(const char *nameserver, char *result_out, size_t result_sz) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "dig @%s +short +time=2 +tries=1 cluster.local 2>/dev/null | head -1",
             nameserver);
    FILE *fp = popen(cmd, "r");
    char buf[128] = {0};
    if (fp) { (void)fgets(buf, sizeof(buf), fp); pclose(fp); }
    buf[strcspn(buf, "\r\n")] = 0;
    if (strlen(buf) > 0)
        snprintf(result_out, result_sz, "resolv OK: %s", buf);
    else
        snprintf(result_out, result_sz, "ERROR query timeout / NXDOMAIN");
}

/* ── nfs_check: showmount or /proc/mounts, fills mount status ──────────── */
void nfs_check(const char *host, char *result_out, size_t result_sz) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "showmount -e %s --no-headers 2>/dev/null | head -1", host);
    FILE *fp = popen(cmd, "r");
    char buf[128] = {0};
    if (fp) { (void)fgets(buf, sizeof(buf), fp); pclose(fp); }
    buf[strcspn(buf, "\r\n")] = 0;
    if (strlen(buf) > 0)
        snprintf(result_out, result_sz, "exports: %s", buf);
    else
        snprintf(result_out, result_sz, "ERROR no exports / unreachable");
}

/* ── write_state: atomic write via tmp rename ────────────────────────── */
void write_state(const char* state_file,
                 const char* mysql_master_host, const char* mysql_master_port,
                 const char* mysql_slave_host,  const char* mysql_slave_port,
                 const char* redis_master_host, const char* redis_master_port,
                 const char* redis_slave_host,  const char* redis_slave_port,
                 const char* status,
                 const char* m_status,    const char* s_status,
                 const char* sync_status, const char* m_gtid, const char* s_gtid,
                 const char* rm_status,   const char* rs_status, const char* r_rep_status,
                 const char* r_detail,    const char* message,
                 const char* mysql_check_ts, const char* redis_check_ts,
                 /* lb */
                 const char* lb_host,  const char* lb_ping_st, const char* lb_ping,
                 const char* lb_check, const char* lb_chk,
                 /* dns */
                 const char* dns_host,  const char* dns_ping_st, const char* dns_ping,
                 const char* dns_check, const char* dns_chk,
                 /* nc1 */
                 const char* nc1_host,  const char* nc1_ping_st, const char* nc1_ping,
                 const char* nc1_check, const char* nc1_chk,
                 /* nc2 */
                 const char* nc2_host,  const char* nc2_ping_st, const char* nc2_ping,
                 const char* nc2_check, const char* nc2_chk,
                 /* nfs */
                 const char* nfs_host,  const char* nfs_ping_st, const char* nfs_ping,
                 const char* nfs_check, const char* nfs_chk)
{
    char tmp_file[PATH_MAX+8];
    snprintf(tmp_file, sizeof(tmp_file), "%s%s", state_file, TMP_STATE_SUFFIX);
    FILE *f = fopen(tmp_file, "w"); if (!f) return;

    char ts[64]; now_str(ts, sizeof(ts));
    fprintf(f,"SYNCMON_TIMESTAMP=\"%s\"\n", ts);
    fprintf(f,"OVERALL_STATUS=\"%s\"\n",           status         ?status         :"UNKNOWN");
    /* MySQL */
    fprintf(f,"MYSQL_MASTER_HOST=\"%s\"\n",        mysql_master_host?mysql_master_host:"");
    fprintf(f,"MYSQL_MASTER_PORT=\"%s\"\n",        mysql_master_port?mysql_master_port:"");
    fprintf(f,"MYSQL_SLAVE_HOST=\"%s\"\n",         mysql_slave_host ?mysql_slave_host :"");
    fprintf(f,"MYSQL_SLAVE_PORT=\"%s\"\n",         mysql_slave_port ?mysql_slave_port :"");
    fprintf(f,"MYSQL_MASTER_STATUS=\"%s\"\n",      m_status       ?m_status       :"UNKNOWN");
    fprintf(f,"MYSQL_SLAVE_STATUS=\"%s\"\n",       s_status       ?s_status       :"UNKNOWN");
    fprintf(f,"MYSQL_SYNC_STATUS=\"%s\"\n",        sync_status    ?sync_status    :"UNKNOWN");
    fprintf(f,"MYSQL_MASTER_GTID=\"%s\"\n",        m_gtid         ?m_gtid         :"");
    fprintf(f,"MYSQL_SLAVE_GTID=\"%s\"\n",         s_gtid         ?s_gtid         :"");
    fprintf(f,"MYSQL_CHECK_TIMESTAMP=\"%s\"\n",    mysql_check_ts ?mysql_check_ts :"unknown");
    /* Redis */
    fprintf(f,"REDIS_MASTER_HOST=\"%s\"\n",        redis_master_host?redis_master_host:"");
    fprintf(f,"REDIS_MASTER_PORT=\"%s\"\n",        redis_master_port?redis_master_port:"");
    fprintf(f,"REDIS_SLAVE_HOST=\"%s\"\n",         redis_slave_host ?redis_slave_host :"");
    fprintf(f,"REDIS_SLAVE_PORT=\"%s\"\n",         redis_slave_port ?redis_slave_port :"");
    fprintf(f,"REDIS_MASTER_STATUS=\"%s\"\n",      rm_status      ?rm_status      :"UNKNOWN");
    fprintf(f,"REDIS_SLAVE_STATUS=\"%s\"\n",       rs_status      ?rs_status      :"UNKNOWN");
    fprintf(f,"REDIS_REPLICATION_STATUS=\"%s\"\n", r_rep_status   ?r_rep_status   :"UNKNOWN");
    fprintf(f,"REDIS_REPLICATION_DETAIL=\"%s\"\n", r_detail       ?r_detail       :"");
    fprintf(f,"REDIS_CHECK_TIMESTAMP=\"%s\"\n",    redis_check_ts ?redis_check_ts :"unknown");
    /* LB */
    fprintf(f,"LB_HOST=\"%s\"\n",              lb_host    ?lb_host    :"");
    fprintf(f,"LB_PING_STATUS=\"%s\"\n",       lb_ping_st ?lb_ping_st :"N/A");
    fprintf(f,"LB_PING=\"%s\"\n",              lb_ping    ?lb_ping    :"");
    fprintf(f,"LB_CHECK=\"%s\"\n",             lb_check   ?lb_check   :"");
    fprintf(f,"LB_CHECK_TIMESTAMP=\"%s\"\n",   lb_chk     ?lb_chk     :"never");
    /* DNS */
    fprintf(f,"DNS_HOST=\"%s\"\n",             dns_host   ?dns_host   :"");
    fprintf(f,"DNS_PING_STATUS=\"%s\"\n",      dns_ping_st?dns_ping_st:"N/A");
    fprintf(f,"DNS_PING=\"%s\"\n",             dns_ping   ?dns_ping   :"");
    fprintf(f,"DNS_CHECK=\"%s\"\n",            dns_check  ?dns_check  :"");
    fprintf(f,"DNS_CHECK_TIMESTAMP=\"%s\"\n",  dns_chk    ?dns_chk    :"never");
    /* NC1 */
    fprintf(f,"NC1_HOST=\"%s\"\n",             nc1_host   ?nc1_host   :"");
    fprintf(f,"NC1_PING_STATUS=\"%s\"\n",      nc1_ping_st?nc1_ping_st:"N/A");
    fprintf(f,"NC1_PING=\"%s\"\n",             nc1_ping   ?nc1_ping   :"");
    fprintf(f,"NC1_CHECK=\"%s\"\n",            nc1_check  ?nc1_check  :"");
    fprintf(f,"NC1_CHECK_TIMESTAMP=\"%s\"\n",  nc1_chk    ?nc1_chk    :"never");
    /* NC2 */
    fprintf(f,"NC2_HOST=\"%s\"\n",             nc2_host   ?nc2_host   :"");
    fprintf(f,"NC2_PING_STATUS=\"%s\"\n",      nc2_ping_st?nc2_ping_st:"N/A");
    fprintf(f,"NC2_PING=\"%s\"\n",             nc2_ping   ?nc2_ping   :"");
    fprintf(f,"NC2_CHECK=\"%s\"\n",            nc2_check  ?nc2_check  :"");
    fprintf(f,"NC2_CHECK_TIMESTAMP=\"%s\"\n",  nc2_chk    ?nc2_chk    :"never");
    /* NFS */
    fprintf(f,"NFS_HOST=\"%s\"\n",             nfs_host   ?nfs_host   :"");
    fprintf(f,"NFS_PING_STATUS=\"%s\"\n",      nfs_ping_st?nfs_ping_st:"N/A");
    fprintf(f,"NFS_PING=\"%s\"\n",             nfs_ping   ?nfs_ping   :"");
    fprintf(f,"NFS_CHECK=\"%s\"\n",            nfs_check  ?nfs_check  :"");
    fprintf(f,"NFS_CHECK_TIMESTAMP=\"%s\"\n",  nfs_chk    ?nfs_chk    :"never");

    fprintf(f,"SYNCMON_MESSAGE=\"%s\"\n", message?message:"");
    fclose(f);
    rename(tmp_file, state_file);
}

int exec_query(const char* cmd, char* out, size_t out_size) {
    FILE* fp = popen(cmd, "r");
    if (!fp) return 0;
    if (fgets(out, out_size, fp)) trim_newline(out); else out[0]='\0';
    return pclose(fp)==0;
}

void get_mysql_gtid(const char* host,const char* port,const char* user,
                    const char* pass,const char* var,char* out) {
    char cmd[MAX_BUFFER], pass_opt[300]={0};
    if (strlen(pass)>0) snprintf(pass_opt,sizeof(pass_opt),"-p'%s'",pass);
    snprintf(cmd,sizeof(cmd),
             "timeout 10 mysql -h %s -P %s -u %s %s -N -B -e 'SELECT @@GLOBAL.%s;' 2>/dev/null",
             host,port,user,pass_opt,var);
    if (!exec_query(cmd,out,256)||strlen(out)==0) {
        snprintf(cmd,sizeof(cmd),
                 "timeout 10 mysql -h %s -P %s -u %s %s -N -B"
                 " -e 'SHOW GLOBAL VARIABLES LIKE \"%s\";' 2>/dev/null | awk '{print $2}'",
                 host,port,user,pass_opt,var);
        exec_query(cmd,out,256);
    }
}

void run_checks(Config *cfg) {
    char mysql_master_status[16]="UNKNOWN", mysql_slave_status[16]="UNKNOWN",
         mysql_sync_status[16]="UNKNOWN";
    char mysql_master_gtid[256]="", mysql_slave_gtid[256]="";
    char redis_master_status[16]="UNKNOWN", redis_slave_status[16]="UNKNOWN",
         redis_rep_status[16]="UNKNOWN";
    char redis_rep_detail[256]="unknown", overall_msg[256]="";
    char mysql_check_ts[64]="unknown", redis_check_ts[64]="unknown";

    /* ── MariaDB ────────────────────────────────────────────────────────────────── */
    if (cfg->enable_mysql_check) {
        char cmd[MAX_BUFFER], pass_opt[300]={0};
        if (strlen(cfg->mysql_password)>0)
            snprintf(pass_opt,sizeof(pass_opt),"-p'%s'",cfg->mysql_password);
        char tmp[64];
        snprintf(cmd,sizeof(cmd),
                 "timeout 10 mysql -h %s -P %s -u %s %s -N -B -e 'SELECT 1;' 2>/dev/null",
                 cfg->mysql_master_host,cfg->mysql_master_port,cfg->mysql_user,pass_opt);
        int master_ok=exec_query(cmd,tmp,sizeof(tmp));
        snprintf(cmd,sizeof(cmd),
                 "timeout 10 mysql -h %s -P %s -u %s %s -N -B -e 'SELECT 1;' 2>/dev/null",
                 cfg->mysql_slave_host,cfg->mysql_slave_port,cfg->mysql_user,pass_opt);
        int slave_ok=exec_query(cmd,tmp,sizeof(tmp));
        if (master_ok) {
            strcpy(mysql_master_status,"OK");
            get_mysql_gtid(cfg->mysql_master_host,cfg->mysql_master_port,
                           cfg->mysql_user,cfg->mysql_password,"gtid_binlog_pos",mysql_master_gtid);
        } else strcpy(mysql_master_status,"ERROR");
        if (slave_ok) {
            strcpy(mysql_slave_status,"OK");
            get_mysql_gtid(cfg->mysql_slave_host,cfg->mysql_slave_port,
                           cfg->mysql_user,cfg->mysql_password,"gtid_slave_pos",mysql_slave_gtid);
        } else strcpy(mysql_slave_status,"ERROR");
        if (master_ok&&slave_ok)
            strcpy(mysql_sync_status,
                   (strlen(mysql_master_gtid)>0&&strcmp(mysql_master_gtid,mysql_slave_gtid)==0)?"OK":"WARN");
        else strcpy(mysql_sync_status,"ERROR");
        now_str(mysql_check_ts, sizeof(mysql_check_ts));
    }

    /* ── Redis ─────────────────────────────────────────────────────────────────── */
    if (cfg->enable_redis_check) {
        char cmd[MAX_BUFFER], out[MAX_BUFFER];
        const char *pass=cfg->redis_password;
        snprintf(cmd,sizeof(cmd),
                 "redis-cli --no-auth-warning -h %s -p %s %s %s PING 2>/dev/null",
                 cfg->redis_master_host,cfg->redis_master_port,
                 strlen(pass)?"-a":"",strlen(pass)?pass:"");
        int r_master_ok=exec_query(cmd,out,sizeof(out));
        snprintf(cmd,sizeof(cmd),
                 "redis-cli --no-auth-warning -h %s -p %s %s %s PING 2>/dev/null",
                 cfg->redis_slave_host,cfg->redis_slave_port,
                 strlen(pass)?"-a":"",strlen(pass)?pass:"");
        int r_slave_ok=exec_query(cmd,out,sizeof(out));
        strcpy(redis_master_status,r_master_ok?"OK":"ERROR");
        strcpy(redis_slave_status, r_slave_ok ?"OK":"ERROR");
        if (r_slave_ok) {
            snprintf(cmd,sizeof(cmd),
                     "redis-cli --no-auth-warning -h %s -p %s %s %s INFO replication 2>/dev/null",
                     cfg->redis_slave_host,cfg->redis_slave_port,
                     strlen(pass)?"-a":"",strlen(pass)?pass:"");
            FILE* fp=popen(cmd,"r");
            if (fp) {
                char line[256],link[16]="?",io[16]="?",host[64]="?";
                int rep_ok=0;
                while (fgets(line,sizeof(line),fp)) {
                    if      (strncmp(line,"master_link_status:",19)==0)
                        {sscanf(line+19,"%s",link);if(strcmp(link,"up")==0)rep_ok=1;}
                    else if (strncmp(line,"master_last_io_seconds_ago:",27)==0) sscanf(line+27,"%s",io);
                    else if (strncmp(line,"master_host:",12)==0)               sscanf(line+12,"%s",host);
                }
                pclose(fp);
                snprintf(redis_rep_detail,sizeof(redis_rep_detail),"link=%s io=%s host=%s",link,io,host);
                strcpy(redis_rep_status,rep_ok?"OK":"WARN");
            }
        } else strcpy(redis_rep_status,"ERROR");
        now_str(redis_check_ts, sizeof(redis_check_ts));
    }

    /* ── Placeholder components ────────────────────────────────────────────────────── */
    char lb_ping_st[16],  lb_ping[64],  lb_check_res[256], lb_chk[64];
    char dns_ping_st[16], dns_ping[64], dns_check_res[256],dns_chk[64];
    char nc1_ping_st[16], nc1_ping[64], nc1_check_res[256],nc1_chk[64];
    char nc2_ping_st[16], nc2_ping[64], nc2_check_res[256],nc2_chk[64];
    char nfs_ping_st[16], nfs_ping[64], nfs_check_res[256],nfs_chk[64];

    /* LB */
    ping_host(cfg->lb_host, lb_ping_st, lb_ping);
    if (strlen(cfg->lb_check_url)>0)
        http_check(cfg->lb_check_url, lb_check_res, sizeof(lb_check_res));
    else {
        char url[600]; snprintf(url,sizeof(url),"http://%s/",cfg->lb_host);
        http_check(url, lb_check_res, sizeof(lb_check_res));
    }
    now_str(lb_chk, sizeof(lb_chk));

    /* DNS */
    ping_host(cfg->dns_host, dns_ping_st, dns_ping);
    dns_check(cfg->dns_host, dns_check_res, sizeof(dns_check_res));
    now_str(dns_chk, sizeof(dns_chk));

    /* NC1 */
    ping_host(cfg->nc1_host, nc1_ping_st, nc1_ping);
    if (strlen(cfg->nc1_check_url)>0)
        http_check(cfg->nc1_check_url, nc1_check_res, sizeof(nc1_check_res));
    else {
        char url[600]; snprintf(url,sizeof(url),"http://%s/status.php",cfg->nc1_host);
        http_check(url, nc1_check_res, sizeof(nc1_check_res));
    }
    now_str(nc1_chk, sizeof(nc1_chk));

    /* NC2 */
    ping_host(cfg->nc2_host, nc2_ping_st, nc2_ping);
    if (strlen(cfg->nc2_check_url)>0)
        http_check(cfg->nc2_check_url, nc2_check_res, sizeof(nc2_check_res));
    else {
        char url[600]; snprintf(url,sizeof(url),"http://%s/status.php",cfg->nc2_host);
        http_check(url, nc2_check_res, sizeof(nc2_check_res));
    }
    now_str(nc2_chk, sizeof(nc2_chk));

    /* NFS */
    ping_host(cfg->nfs_host, nfs_ping_st, nfs_ping);
    nfs_check(cfg->nfs_host, nfs_check_res, sizeof(nfs_check_res));
    now_str(nfs_chk, sizeof(nfs_chk));

    /* ── Overall status ──────────────────────────────────────────────────────────────── */
    char overall[16];
    if (strcmp(mysql_master_status,"OK")==0 && strcmp(mysql_slave_status,"OK")==0 &&
        strcmp(mysql_sync_status,  "OK")==0 &&
        strcmp(redis_master_status,"OK")==0 && strcmp(redis_slave_status, "OK")==0 &&
        strcmp(redis_rep_status,   "OK")==0) {
        strcpy(overall,"OK"); strcpy(overall_msg,"All systems operational");
    } else if (strcmp(mysql_master_status,"ERROR")==0 && strcmp(redis_master_status,"ERROR")==0) {
        strcpy(overall,"ERROR"); strcpy(overall_msg,"Multiple master failures detected");
    } else {
        strcpy(overall,"WARN"); strcpy(overall_msg,"One or more components degraded");
    }

    write_state(cfg->state_file,
                cfg->mysql_master_host, cfg->mysql_master_port,
                cfg->mysql_slave_host,  cfg->mysql_slave_port,
                cfg->redis_master_host, cfg->redis_master_port,
                cfg->redis_slave_host,  cfg->redis_slave_port,
                overall,
                mysql_master_status, mysql_slave_status, mysql_sync_status,
                mysql_master_gtid,   mysql_slave_gtid,
                redis_master_status, redis_slave_status, redis_rep_status,
                redis_rep_detail,    overall_msg,
                mysql_check_ts,      redis_check_ts,
                cfg->lb_host,  lb_ping_st,  lb_ping,  lb_check_res,  lb_chk,
                cfg->dns_host, dns_ping_st, dns_ping, dns_check_res, dns_chk,
                cfg->nc1_host, nc1_ping_st, nc1_ping, nc1_check_res, nc1_chk,
                cfg->nc2_host, nc2_ping_st, nc2_ping, nc2_check_res, nc2_chk,
                cfg->nfs_host, nfs_ping_st, nfs_ping, nfs_check_res, nfs_chk);

    char log_buf[512];
    snprintf(log_buf,sizeof(log_buf),"check complete: %s",overall);
    log_message(cfg,log_buf);
}

void run_test_mode(const char* state_file) {
    srand(time(NULL));
    const char* statuses[]={"OK","WARN","ERROR"};
    const char* base_gtid=(rand()%2)
        ?"3E111111-1111-1111-1111-111111111111"
        :"4F222222-2222-2222-2222-222222222222";
    int mg=(rand()%10000)+1000, sg=mg;
    if ((rand()%100)<30) sg-=(rand()%50+1);
    char m_gtid_str[256],s_gtid_str[256],r_detail[256],ts[64];
    snprintf(m_gtid_str,sizeof(m_gtid_str),"%s:%d",base_gtid,mg);
    snprintf(s_gtid_str,sizeof(s_gtid_str),"%s:%d",base_gtid,sg);
    const char* rep_status=statuses[rand()%3];
    snprintf(r_detail,sizeof(r_detail),"link=%s io=%d host=172.31.%d.%d",
             strcmp(rep_status,"ERROR")==0?"down":"up",rand()%15,rand()%255,rand()%255);
    now_str(ts, sizeof(ts));

    /* mock ping helper */
    const char* ping_labels[]={"3ms","12ms","251ms","timeout"};
    const char* ping_st[]    ={"OK", "OK",  "WARN", "ERROR"};
#define RPICK(arr) arr[rand()%4]

    write_state(state_file,
                "172.31.49.233","3306","172.31.40.234","3306",
                "172.31.40.234","6379","172.31.40.233","6379",
                statuses[rand()%3],
                statuses[rand()%3],statuses[rand()%3],
                mg==sg?"OK":"WARN",
                m_gtid_str,s_gtid_str,
                statuses[rand()%3],statuses[rand()%3],
                rep_status,r_detail,"Live Simulation Data",
                ts,ts,
                /* lb  */ "172.31.0.10",  RPICK(ping_st), RPICK(ping_labels),
                          "HAProxy active, backend 3/3 up", ts,
                /* dns */ "172.31.0.53",  RPICK(ping_st), RPICK(ping_labels),
                          "resolv OK: 172.31.0.1", ts,
                /* nc1 */ "172.31.1.11",  RPICK(ping_st), RPICK(ping_labels),
                          "HTTP 200 /status.php maintenance=false", ts,
                /* nc2 */ "172.31.1.12",  RPICK(ping_st), RPICK(ping_labels),
                          "HTTP 200 /status.php maintenance=false", ts,
                /* nfs */ "172.31.2.20",  RPICK(ping_st), RPICK(ping_labels),
                          "exports: /data/shared 172.31.0.0/24(ro,sync)", ts);
#undef RPICK
    printf("Simulated test state written to %s\n", state_file);
    exit(0);
}

int main(int argc, char *argv[]) {
    Config cfg; init_config(&cfg);
    for (int i=1;i<argc;i++) {
        if      (strcmp(argv[i],"--test")==0)  run_test_mode(cfg.state_file);
        else if (strcmp(argv[i],"--help")==0||strcmp(argv[i],"-h")==0) {
            printf("Usage: syncmon-daemon [--test] [--help]\n\n"
                   "  --test    Write a sample syncmon state file and exit\n"
                   "  --help    Show this usage message\n\n"
                   "Config  : %s\nLog     : %s\nState   : %s\n",
                   CONFIG_FILE_DEFAULT,LOG_FILE_DEFAULT,STATE_FILE_DEFAULT);
            return 0;
        } else { fprintf(stderr,"ERROR: unknown argument: %s\n",argv[i]); return 1; }
    }
    ensure_log_dir(cfg.log_file); ensure_log_dir(cfg.state_file);
    load_config(CONFIG_FILE_DEFAULT,&cfg);
    ensure_log_dir(cfg.log_file); ensure_log_dir(cfg.state_file);
    if (cfg.startup_check) { log_message(&cfg,"Performing initial startup checks..."); run_checks(&cfg); }
    char startup_msg[PATH_MAX+128];
    snprintf(startup_msg,sizeof(startup_msg),
             "SyncMon daemon starting (interval=%ds, state=%s)",
             cfg.check_interval,cfg.state_file);
    log_message(&cfg,startup_msg);
    while (1) { run_checks(&cfg); sleep(cfg.check_interval); }
    return 0;
}

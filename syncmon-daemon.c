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
                 /* Redis payload tracing */
                 const char* redis_sent_payload, const char* redis_recv_payload,
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
    /* Redis payload tracing: what master wrote vs what slave echoed back */
    fprintf(f,"REDIS_SENT_PAYLOAD=\"%s\"\n",       redis_sent_payload ?redis_sent_payload :"");
    fprintf(f,"REDIS_RECV_PAYLOAD=\"%s\"\n",       redis_recv_payload ?redis_recv_payload :"");
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

/* ── Global Redis payload counter (persists across check cycles) ─────── */
static unsigned long long g_redis_seq = 0;

void run_checks(Config *cfg) {
    char mysql_master_status[16]="UNKNOWN", mysql_slave_status[16]="UNKNOWN",
         mysql_sync_status[16]="UNKNOWN";
    char mysql_master_gtid[256]="", mysql_slave_gtid[256]="";
    char redis_master_status[16]="UNKNOWN", redis_slave_status[16]="UNKNOWN",
         redis_rep_status[16]="UNKNOWN";
    char redis_rep_detail[256]="unknown", overall_msg[256]="";
    char mysql_check_ts[64]="unknown", redis_check_ts[64]="unknown";
    /* Redis payload tracing */
    char redis_sent_payload[128]="", redis_recv_payload[128]="";

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
        char auth_args[128]="";
        if (strlen(pass)>0) snprintf(auth_args,sizeof(auth_args),"-a %s",pass);

        /* Build payload: upcounting sequence number + current timestamp */
        char ts_now[64]; now_str(ts_now, sizeof(ts_now));
        g_redis_seq++;
        snprintf(redis_sent_payload, sizeof(redis_sent_payload),
                 "seq=%llu ts=%s", g_redis_seq, ts_now);

        /* Write payload to master */
        snprintf(cmd,sizeof(cmd),
                 "redis-cli --no-auth-warning -h %s -p %s %s SET syncmon:probe \"%s\" EX 120 2>/dev/null",
                 cfg->redis_master_host, cfg->redis_master_port,
                 auth_args, redis_sent_payload);
        int r_master_ok = (system(cmd) == 0);
        strcpy(redis_master_status, r_master_ok ? "OK" : "ERROR");

        /* Read payload back from slave (confirms replication round-trip) */
        snprintf(cmd,sizeof(cmd),
                 "redis-cli --no-auth-warning -h %s -p %s %s GET syncmon:probe 2>/dev/null",
                 cfg->redis_slave_host, cfg->redis_slave_port,
                 auth_args);
        int r_slave_ok = exec_query(cmd, out, sizeof(out));
        strcpy(redis_slave_status, r_slave_ok ? "OK" : "ERROR");

        if (r_slave_ok && strlen(out) > 0) {
            snprintf(redis_recv_payload, sizeof(redis_recv_payload), "%s", out);
        } else {
            snprintf(redis_recv_payload, sizeof(redis_recv_payload), "(no data)");
        }

        if (r_slave_ok) {
            /* Also check INFO replication for link status */
            snprintf(cmd,sizeof(cmd),
                     "redis-cli --no-auth-warning -h %s -p %s %s INFO replication 2>/dev/null",
                     cfg->redis_slave_host,cfg->redis_slave_port, auth_args);
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
                /* Verify payload echo matches what we sent */
                int payload_ok = (strcmp(redis_sent_payload, redis_recv_payload)==0);
                snprintf(redis_rep_detail,sizeof(redis_rep_detail),
                         "link=%s io=%s host=%s payload=%s",
                         link, io, host, payload_ok ? "match" : "mismatch");
                strcpy(redis_rep_status, (rep_ok && payload_ok) ? "OK" : "WARN");
            }
        } else {
            strcpy(redis_rep_status,"ERROR");
            snprintf(redis_rep_detail,sizeof(redis_rep_detail),"slave unreachable");
        }
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
                redis_sent_payload,  redis_recv_payload,
                cfg->lb_host,  lb_ping_st,  lb_ping,  lb_check_res,  lb_chk,
                cfg->dns_host, dns_ping_st, dns_ping, dns_check_res, dns_chk,
                cfg->nc1_host, nc1_ping_st, nc1_ping, nc1_check_res, nc1_chk,
                cfg->nc2_host, nc2_ping_st, nc2_ping, nc2_check_res, nc2_chk,
                cfg->nfs_host, nfs_ping_st, nfs_ping, nfs_check_res, nfs_chk);

    char log_buf[512];
    snprintf(log_buf,sizeof(log_buf),"check complete: %s (redis probe seq=%llu)",
             overall, g_redis_seq);
    log_message(cfg,log_buf);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Realistic simulation state machine
 *
 * The system starts fully healthy. Every ~30 s tick it advances through a
 * scenario list that mimics what a real cluster experiences over time:
 *   - Long stretches of normal operation (OK everywhere)
 *   - Brief transient latency spikes (WARN) on individual components
 *   - Short replication lag periods (slave gtid behind master by a few txns)
 *   - Occasional single-component errors (~30 s, one or two ticks)
 *   - Recovery back to OK
 *
 * Key causality rules that are enforced:
 *   - If redis_master is ERROR  → redis_rep is ERROR, slave read is stale/none
 *   - If redis_slave  is ERROR  → redis_rep is ERROR
 *   - If mysql_master is ERROR  → mysql_sync is ERROR, slave gtid may drift
 *   - mysql_sync WARN only when both nodes are reachable but gtids differ
 *   - ping WARN/ERROR on a host cascades to its service status logically
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    /* description logged */
    const char *scenario_name;
    /* MySQL */
    int mysql_master_ok;    /* 1=OK, 0=ERROR */
    int mysql_slave_ok;
    int mysql_gtid_lag;     /* slave is N transactions behind master */
    /* Redis */
    int redis_master_ok;
    int redis_slave_ok;
    int redis_link_up;      /* 0 = link down even if both respond to PING */
    int redis_io_sec;       /* master_last_io_seconds_ago */
    int redis_payload_match;/* 1 = sent==recv, 0 = stale/mismatch */
    /* LB */
    int lb_ping_ms;         /* -1 = timeout */
    int lb_http_code;       /* 0 = curl error */
    int lb_backends;        /* e.g. 3/3, 2/3 */
    /* DNS */
    int dns_ping_ms;
    int dns_resolv_ok;
    /* NC1 */
    int nc1_ping_ms;
    int nc1_http_code;
    int nc1_maintenance;
    /* NC2 */
    int nc2_ping_ms;
    int nc2_http_code;
    int nc2_maintenance;
    /* NFS */
    int nfs_ping_ms;
    int nfs_export_ok;
    /* How many simulation ticks to hold this scenario */
    int ticks;
} SimScenario;

static const SimScenario SIM_SCENARIOS[] = {
    /* name,               mm  ms  lag  rm  rs  lnk  io  pm   lb_ms lb_c lb_b  dn_ms dn_r  n1ms n1c n1m  n2ms n2c n2m  nfs_ms nfs_e  ticks */
    {"Healthy baseline",    1,  1,  0,  1,  1,  1,   1,  1,    2, 200,  3,    2,  1,    2, 200,  0,   2, 200,  0,   2,    1,    10},
    {"Healthy baseline",    1,  1,  0,  1,  1,  1,   1,  1,    3, 200,  3,    3,  1,    3, 200,  0,   3, 200,  0,   3,    1,     8},
    {"Healthy baseline",    1,  1,  0,  1,  1,  1,   2,  1,    4, 200,  3,    2,  1,    4, 200,  0,   4, 200,  0,   4,    1,     6},
    {"LB latency spike",    1,  1,  0,  1,  1,  1,   1,  1,  280, 200,  3,    3,  1,    3, 200,  0,   3, 200,  0,   3,    1,     2},
    {"Healthy baseline",    1,  1,  0,  1,  1,  1,   1,  1,    5, 200,  3,    3,  1,    4, 200,  0,   4, 200,  0,   3,    1,     5},
    {"Redis slave io lag",  1,  1,  0,  1,  1,  1,  12,  1,    4, 200,  3,    2,  1,    3, 200,  0,   3, 200,  0,   3,    1,     2},
    {"Redis io lag higher", 1,  1,  0,  1,  1,  1,  28,  1,    4, 200,  3,    2,  1,    3, 200,  0,   3, 200,  0,   3,    1,     1},
    {"Redis link down",     1,  1,  0,  1,  1,  0,  -1,  0,    4, 200,  3,    3,  1,    4, 200,  0,   4, 200,  0,   4,    1,     1},
    {"Redis link recover",  1,  1,  0,  1,  1,  1,   3,  1,    3, 200,  3,    3,  1,    3, 200,  0,   3, 200,  0,   3,    1,     2},
    {"Healthy baseline",    1,  1,  0,  1,  1,  1,   1,  1,    3, 200,  3,    2,  1,    3, 200,  0,   3, 200,  0,   3,    1,     8},
    {"MySQL gtid lag",      1,  1,  3,  1,  1,  1,   1,  1,    3, 200,  3,    2,  1,    3, 200,  0,   3, 200,  0,   3,    1,     2},
    {"MySQL gtid lag",      1,  1,  1,  1,  1,  1,   1,  1,    3, 200,  3,    2,  1,    3, 200,  0,   3, 200,  0,   3,    1,     1},
    {"MySQL gtid synced",   1,  1,  0,  1,  1,  1,   1,  1,    3, 200,  3,    2,  1,    3, 200,  0,   3, 200,  0,   3,    1,     3},
    {"Healthy baseline",    1,  1,  0,  1,  1,  1,   1,  1,    4, 200,  3,    3,  1,    4, 200,  0,   4, 200,  0,   4,    1,     6},
    {"NC2 maintenance",     1,  1,  0,  1,  1,  1,   2,  1,    3, 200,  3,    2,  1,    3, 200,  0,   3, 200,  1,   3,    1,     2},
    {"NC2 back online",     1,  1,  0,  1,  1,  1,   1,  1,    3, 200,  3,    2,  1,    3, 200,  0,   3, 200,  0,   3,    1,     3},
    {"Healthy baseline",    1,  1,  0,  1,  1,  1,   1,  1,    4, 200,  3,    3,  1,    4, 200,  0,   4, 200,  0,   4,    1,     8},
    {"NFS latency spike",   1,  1,  0,  1,  1,  1,   1,  1,    3, 200,  3,    3,  1,    3, 200,  0,   3, 200,  0, 340,    1,     1},
    {"NFS unreachable",     1,  1,  0,  1,  1,  1,   1,  1,    3, 200,  3,    3,  1,    3, 200,  0,   3, 200,  0,  -1,    0,     1},
    {"NFS recover",         1,  1,  0,  1,  1,  1,   1,  1,    3, 200,  3,    3,  1,    3, 200,  0,   3, 200,  0,   5,    1,     2},
    {"Healthy baseline",    1,  1,  0,  1,  1,  1,   1,  1,    4, 200,  3,    3,  1,    4, 200,  0,   4, 200,  0,   4,    1,     8},
    {"Redis slave down",    1,  1,  0,  1,  0,  0,  -1,  0,    4, 200,  3,    3,  1,    4, 200,  0,   4, 200,  0,   4,    1,     1},
    {"Redis slave recover", 1,  1,  0,  1,  1,  1,   5,  1,    4, 200,  3,    3,  1,    4, 200,  0,   4, 200,  0,   4,    1,     2},
    {"Healthy baseline",    1,  1,  0,  1,  1,  1,   1,  1,    4, 200,  3,    3,  1,    4, 200,  0,   4, 200,  0,   4,    1,     6},
    {"MySQL slave down",    1,  0,  0,  1,  1,  1,   1,  1,    4, 200,  3,    3,  1,    4, 200,  0,   4, 200,  0,   4,    1,     1},
    {"MySQL slave recover", 1,  1,  5,  1,  1,  1,   1,  1,    4, 200,  3,    3,  1,    4, 200,  0,   4, 200,  0,   4,    1,     1},
    {"MySQL gtid catchup",  1,  1,  2,  1,  1,  1,   1,  1,    4, 200,  3,    3,  1,    4, 200,  0,   4, 200,  0,   4,    1,     1},
    {"MySQL synced again",  1,  1,  0,  1,  1,  1,   1,  1,    4, 200,  3,    3,  1,    4, 200,  0,   4, 200,  0,   4,    1,     5},
    {"Healthy baseline",    1,  1,  0,  1,  1,  1,   1,  1,    3, 200,  3,    2,  1,    3, 200,  0,   3, 200,  0,   3,    1,     6},
    {"LB one backend down", 1,  1,  0,  1,  1,  1,   2,  1,    5, 200,  2,    3,  1,    4, 200,  0,   4, 200,  0,   4,    1,     2},
    {"LB recovered",        1,  1,  0,  1,  1,  1,   1,  1,    4, 200,  3,    3,  1,    4, 200,  0,   4, 200,  0,   4,    1,     4},
    {"Healthy baseline",    1,  1,  0,  1,  1,  1,   1,  1,    4, 200,  3,    3,  1,    4, 200,  0,   4, 200,  0,   4,    1,    10},
};
#define SIM_SCENARIO_COUNT ((int)(sizeof(SIM_SCENARIOS)/sizeof(SIM_SCENARIOS[0])))

void run_test_mode(const char* state_file) {
    static int sim_scenario_idx  = -1;  /* current scenario index */
    static int sim_tick_in_scenario = 0;

    /* On first call, load persistent state from a tiny side-file */
    char sim_state_path[PATH_MAX+16];
    snprintf(sim_state_path, sizeof(sim_state_path), "%s.simstate", state_file);

    if (sim_scenario_idx < 0) {
        FILE *sf = fopen(sim_state_path, "r");
        if (sf) {
            int idx=0, tick=0;
            unsigned long long seq=0;
            if (fscanf(sf, "%d %d %llu", &idx, &tick, &seq) == 3) {
                sim_scenario_idx    = idx  % SIM_SCENARIO_COUNT;
                sim_tick_in_scenario = tick;
                g_redis_seq          = seq;
            }
            fclose(sf);
        } else {
            sim_scenario_idx    = 0;
            sim_tick_in_scenario = 0;
            g_redis_seq          = 0;
        }
    }

    /* Advance to next scenario if ticks exhausted */
    const SimScenario *sc = &SIM_SCENARIOS[sim_scenario_idx];
    if (sim_tick_in_scenario >= sc->ticks) {
        sim_scenario_idx = (sim_scenario_idx + 1) % SIM_SCENARIO_COUNT;
        sim_tick_in_scenario = 0;
        sc = &SIM_SCENARIOS[sim_scenario_idx];
    }
    sim_tick_in_scenario++;

    /* Bump redis probe sequence */
    g_redis_seq++;

    /* ── Build timestamp ─────────────────────────────────────────────── */
    char ts[64]; now_str(ts, sizeof(ts));

    /* ── Build Redis sent payload ─────────────────────────────────────── */
    char redis_sent[128], redis_recv[128];
    snprintf(redis_sent, sizeof(redis_sent), "seq=%llu ts=%s", g_redis_seq, ts);
    if (sc->redis_payload_match && sc->redis_slave_ok && sc->redis_link_up) {
        snprintf(redis_recv, sizeof(redis_recv), "%s", redis_sent);
    } else if (sc->redis_slave_ok && !sc->redis_link_up) {
        /* slave responds but holds stale data from last good replication */
        snprintf(redis_recv, sizeof(redis_recv), "seq=%llu ts=(stale)",
                 g_redis_seq > 1 ? g_redis_seq - 1 : g_redis_seq);
    } else {
        snprintf(redis_recv, sizeof(redis_recv), "(no data)");
    }

    /* ── MySQL GTID ───────────────────────────────────────────────────── */
    unsigned long long base_txn = 10000 + g_redis_seq * 3;
    char m_gtid[256], s_gtid[256];
    snprintf(m_gtid, sizeof(m_gtid), "3E11-1111-1111-1111-1111:%llu", base_txn);
    snprintf(s_gtid, sizeof(s_gtid), "3E11-1111-1111-1111-1111:%llu",
             sc->mysql_slave_ok ? (base_txn - (unsigned long long)sc->mysql_gtid_lag)
                                : (base_txn - 10ULL));

    /* ── Derive statuses ──────────────────────────────────────────────── */
    const char *mysql_master_st = sc->mysql_master_ok ? "OK" : "ERROR";
    const char *mysql_slave_st  = sc->mysql_slave_ok  ? "OK" : "ERROR";
    const char *mysql_sync_st;
    if (!sc->mysql_master_ok || !sc->mysql_slave_ok)
        mysql_sync_st = "ERROR";
    else if (sc->mysql_gtid_lag > 0)
        mysql_sync_st = "WARN";
    else
        mysql_sync_st = "OK";

    const char *redis_master_st = sc->redis_master_ok ? "OK" : "ERROR";
    const char *redis_slave_st  = sc->redis_slave_ok  ? "OK" : "ERROR";
    const char *redis_rep_st;
    char redis_detail[256];
    if (!sc->redis_master_ok || !sc->redis_slave_ok) {
        redis_rep_st = "ERROR";
        snprintf(redis_detail, sizeof(redis_detail),
                 "link=down io=N/A host=172.31.40.234");
    } else if (!sc->redis_link_up) {
        redis_rep_st = "WARN";
        snprintf(redis_detail, sizeof(redis_detail),
                 "link=down io=N/A host=172.31.40.234");
    } else if (sc->redis_io_sec > 20) {
        redis_rep_st = "WARN";
        snprintf(redis_detail, sizeof(redis_detail),
                 "link=up io=%d host=172.31.40.234 payload=match", sc->redis_io_sec);
    } else {
        int pmatch = sc->redis_payload_match;
        redis_rep_st = pmatch ? "OK" : "WARN";
        snprintf(redis_detail, sizeof(redis_detail),
                 "link=up io=%d host=172.31.40.234 payload=%s",
                 sc->redis_io_sec, pmatch ? "match" : "mismatch");
    }

    /* ── Ping helpers ─────────────────────────────────────────────────── */
#define PING_ST(ms)  ((ms)<0?"ERROR":(ms)<100?"OK":(ms)<500?"WARN":"ERROR")
#define PING_STR(ms) ((ms)<0?"timeout":"")

    char lb_ping_str[16], dns_ping_str[16], nc1_ping_str[16],
         nc2_ping_str[16], nfs_ping_str[16];
    if (sc->lb_ping_ms  >= 0) snprintf(lb_ping_str,  sizeof(lb_ping_str),  "%dms", sc->lb_ping_ms);
    else strcpy(lb_ping_str,  "timeout");
    if (sc->dns_ping_ms >= 0) snprintf(dns_ping_str, sizeof(dns_ping_str), "%dms", sc->dns_ping_ms);
    else strcpy(dns_ping_str, "timeout");
    if (sc->nc1_ping_ms >= 0) snprintf(nc1_ping_str, sizeof(nc1_ping_str), "%dms", sc->nc1_ping_ms);
    else strcpy(nc1_ping_str, "timeout");
    if (sc->nc2_ping_ms >= 0) snprintf(nc2_ping_str, sizeof(nc2_ping_str), "%dms", sc->nc2_ping_ms);
    else strcpy(nc2_ping_str, "timeout");
    if (sc->nfs_ping_ms >= 0) snprintf(nfs_ping_str, sizeof(nfs_ping_str), "%dms", sc->nfs_ping_ms);
    else strcpy(nfs_ping_str, "timeout");

    /* ── Service check strings ────────────────────────────────────────── */
    char lb_check[128], nc1_check[128], nc2_check[128], nfs_check[128], dns_check[128];

    /* LB: degrade http if ping failed */
    if (sc->lb_ping_ms < 0 || sc->lb_http_code == 0)
        snprintf(lb_check, sizeof(lb_check), "curl error / unreachable");
    else
        snprintf(lb_check, sizeof(lb_check), "HTTP %d HAProxy active, backend %d/3 up",
                 sc->lb_http_code, sc->lb_backends);

    /* NC1 */
    if (sc->nc1_ping_ms < 0 || sc->nc1_http_code == 0)
        snprintf(nc1_check, sizeof(nc1_check), "curl error / unreachable");
    else
        snprintf(nc1_check, sizeof(nc1_check), "HTTP %d /status.php maintenance=%s",
                 sc->nc1_http_code, sc->nc1_maintenance ? "true" : "false");

    /* NC2 */
    if (sc->nc2_ping_ms < 0 || sc->nc2_http_code == 0)
        snprintf(nc2_check, sizeof(nc2_check), "curl error / unreachable");
    else
        snprintf(nc2_check, sizeof(nc2_check), "HTTP %d /status.php maintenance=%s",
                 sc->nc2_http_code, sc->nc2_maintenance ? "true" : "false");

    /* NFS */
    if (sc->nfs_ping_ms < 0 || !sc->nfs_export_ok)
        snprintf(nfs_check, sizeof(nfs_check), "ERROR no exports / unreachable");
    else
        snprintf(nfs_check, sizeof(nfs_check), "exports: /data/shared 172.31.0.0/24(ro,sync)");

    /* DNS */
    if (sc->dns_ping_ms < 0 || !sc->dns_resolv_ok)
        snprintf(dns_check, sizeof(dns_check), "ERROR query timeout / NXDOMAIN");
    else
        snprintf(dns_check, sizeof(dns_check), "resolv OK: 172.31.0.1");

    /* ── Overall ──────────────────────────────────────────────────────── */
    const char *overall;
    const char *msg;
    if (strcmp(mysql_master_st,"OK")==0 && strcmp(mysql_slave_st,"OK")==0 &&
        strcmp(mysql_sync_st,  "OK")==0 &&
        strcmp(redis_master_st,"OK")==0 && strcmp(redis_slave_st, "OK")==0 &&
        strcmp(redis_rep_st,   "OK")==0) {
        overall = "OK";  msg = "All systems operational";
    } else if (strcmp(mysql_master_st,"ERROR")==0 && strcmp(redis_master_st,"ERROR")==0) {
        overall = "ERROR"; msg = "Multiple master failures detected";
    } else {
        overall = "WARN";  msg = sc->scenario_name;
    }

    write_state(state_file,
                "172.31.49.233","3306","172.31.40.234","3306",
                "172.31.40.234","6379","172.31.40.233","6379",
                overall,
                mysql_master_st, mysql_slave_st, mysql_sync_st,
                m_gtid, s_gtid,
                redis_master_st, redis_slave_st, redis_rep_st,
                redis_detail, msg,
                ts, ts,
                redis_sent, redis_recv,
                /* lb  */ "172.31.0.10",  PING_ST(sc->lb_ping_ms),  lb_ping_str,  lb_check,  ts,
                /* dns */ "172.31.0.53",  PING_ST(sc->dns_ping_ms), dns_ping_str, dns_check, ts,
                /* nc1 */ "172.31.1.11",  PING_ST(sc->nc1_ping_ms), nc1_ping_str, nc1_check, ts,
                /* nc2 */ "172.31.1.12",  PING_ST(sc->nc2_ping_ms), nc2_ping_str, nc2_check, ts,
                /* nfs */ "172.31.2.20",  PING_ST(sc->nfs_ping_ms), nfs_ping_str, nfs_check, ts);
#undef PING_ST
#undef PING_STR

    /* Persist simulation state for next tick */
    FILE *sf = fopen(sim_state_path, "w");
    if (sf) {
        fprintf(sf, "%d %d %llu\n", sim_scenario_idx, sim_tick_in_scenario, g_redis_seq);
        fclose(sf);
    }

    printf("[sim tick %d/%d] scenario=%d \"%s\" redis_seq=%llu overall=%s\n",
           sim_tick_in_scenario, sc->ticks,
           sim_scenario_idx, sc->scenario_name,
           g_redis_seq, overall);
}

int main(int argc, char *argv[]) {
    Config cfg; init_config(&cfg);
    int test_mode = 0;
    for (int i=1;i<argc;i++) {
        if      (strcmp(argv[i],"--test")==0)  test_mode = 1;
        else if (strcmp(argv[i],"--help")==0||strcmp(argv[i],"-h")==0) {
            printf("Usage: syncmon-daemon [--test] [--help]\n\n"
                   "  --test    Run as realistic simulation (writes state each interval, loops)\n"
                   "  --help    Show this usage message\n\n"
                   "Config  : %s\nLog     : %s\nState   : %s\n",
                   CONFIG_FILE_DEFAULT,LOG_FILE_DEFAULT,STATE_FILE_DEFAULT);
            return 0;
        } else { fprintf(stderr,"ERROR: unknown argument: %s\n",argv[i]); return 1; }
    }

    ensure_log_dir(cfg.log_file); ensure_log_dir(cfg.state_file);

    if (test_mode) {
        /* Load config if available (non-fatal) */
        FILE *tcf = fopen(CONFIG_FILE_DEFAULT, "r");
        if (tcf) { fclose(tcf); load_config(CONFIG_FILE_DEFAULT, &cfg); }
        printf("SyncMon simulation running (interval=%ds, state=%s)\n"
               "Press Ctrl-C to stop.\n",
               cfg.check_interval, cfg.state_file);
        while (1) {
            run_test_mode(cfg.state_file);
            sleep(cfg.check_interval);
        }
        return 0;
    }

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

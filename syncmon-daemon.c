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

#define STATE_FILE_DEFAULT "/tmp/syncmon_state.env"
#define LOG_FILE_DEFAULT "/tmp/syncmon.log"
#define TMP_STATE_SUFFIX ".tmp"
#define MAX_BUFFER 2048

// Configuration structure matching bash script defaults
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
} Config;

void init_config(Config *cfg) {
    cfg->check_interval = 30;
    cfg->enable_mysql_check = 1;
    cfg->enable_redis_check = 1;
    cfg->log_max_size = 100;
    cfg->startup_check = 1;
    cfg->exit_on_startup_failure = 0;
    snprintf(cfg->log_file, PATH_MAX, "%s", LOG_FILE_DEFAULT);
    const char* env_state = getenv("STATE_FILE");
    snprintf(cfg->state_file, PATH_MAX, "%s", env_state ? env_state : STATE_FILE_DEFAULT);
}

void trim_newline(char *str) {
    str[strcspn(str, "\r\n")] = 0;
}

void load_config(const char *filepath, Config *cfg) {
    FILE *f = fopen(filepath, "r");
    if (!f) {
        fprintf(stderr, "ERROR: configuration file not found: %s\n", filepath);
        exit(1);
    }
    char line[MAX_BUFFER];
    while (fgets(line, sizeof(line), f)) {
        char *p = strchr(line, '#');
        if (p) *p = 0; // Strip comments
        trim_newline(line);
        if (strlen(line) == 0) continue;

        char key[256] = {0}, val[256] = {0};
        if (sscanf(line, "%255[^=]=%255[^\n]", key, val) == 2) {
            // Remove surrounding quotes if present
            if ((val[0] == '"' || val[0] == '\'') && val[strlen(val)-1] == val[0]) {
                val[strlen(val)-1] = '\0';
                memmove(val, val+1, strlen(val));
            }
            if (strcmp(key, "CHECK_INTERVAL") == 0) cfg->check_interval = atoi(val);
            else if (strcmp(key, "ENABLE_MYSQL_CHECK") == 0) cfg->enable_mysql_check = atoi(val);
            else if (strcmp(key, "ENABLE_REDIS_CHECK") == 0) cfg->enable_redis_check = atoi(val);
            else if (strcmp(key, "LOG_MAX_SIZE") == 0) cfg->log_max_size = atoi(val);
            else if (strcmp(key, "LOG_FILE") == 0) snprintf(cfg->log_file, sizeof(cfg->log_file), "%s", val);
            else if (strcmp(key, "STARTUP_CHECK") == 0) cfg->startup_check = atoi(val);
            else if (strcmp(key, "EXIT_ON_STARTUP_FAILURE") == 0) cfg->exit_on_startup_failure = atoi(val);
            else if (strcmp(key, "MYSQL_MASTER_HOST") == 0) snprintf(cfg->mysql_master_host, sizeof(cfg->mysql_master_host), "%s", val);
            else if (strcmp(key, "MYSQL_MASTER_PORT") == 0) snprintf(cfg->mysql_master_port, sizeof(cfg->mysql_master_port), "%s", val);
            else if (strcmp(key, "MYSQL_SLAVE_HOST") == 0) snprintf(cfg->mysql_slave_host, sizeof(cfg->mysql_slave_host), "%s", val);
            else if (strcmp(key, "MYSQL_SLAVE_PORT") == 0) snprintf(cfg->mysql_slave_port, sizeof(cfg->mysql_slave_port), "%s", val);
            else if (strcmp(key, "MYSQL_USER") == 0) snprintf(cfg->mysql_user, sizeof(cfg->mysql_user), "%s", val);
            else if (strcmp(key, "MYSQL_PASSWORD") == 0) snprintf(cfg->mysql_password, sizeof(cfg->mysql_password), "%s", val);
            else if (strcmp(key, "REDIS_MASTER_HOST") == 0) snprintf(cfg->redis_master_host, sizeof(cfg->redis_master_host), "%s", val);
            else if (strcmp(key, "REDIS_MASTER_PORT") == 0) snprintf(cfg->redis_master_port, sizeof(cfg->redis_master_port), "%s", val);
            else if (strcmp(key, "REDIS_SLAVE_HOST") == 0) snprintf(cfg->redis_slave_host, sizeof(cfg->redis_slave_host), "%s", val);
            else if (strcmp(key, "REDIS_SLAVE_PORT") == 0) snprintf(cfg->redis_slave_port, sizeof(cfg->redis_slave_port), "%s", val);
            else if (strcmp(key, "REDIS_PASSWORD") == 0) snprintf(cfg->redis_password, sizeof(cfg->redis_password), "%s", val);
        }
    }
    fclose(f);
}

void log_message(Config *cfg, const char *msg) {
    if (cfg->log_max_size > 0) {
        struct stat st;
        if (stat(cfg->log_file, &st) == 0 && st.st_size > (cfg->log_max_size * 1024 * 1024)) {
            char old_log[PATH_MAX + 8]; 
            snprintf(old_log, sizeof(old_log), "%s.old", cfg->log_file);
            rename(cfg->log_file, old_log);
        }
    }
    FILE *f = fopen(cfg->log_file, "a");
    if (f) {
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);
        fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d %s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, msg);
        fclose(f);
    }
}

void write_state(const char* state_file, const char* status, const char* m_status, const char* s_status,
                 const char* sync_status, const char* m_gtid, const char* s_gtid,
                 const char* rm_status, const char* rs_status, const char* r_rep_status,
                 const char* r_detail, const char* message,
                 const char* mysql_check_ts, const char* redis_check_ts) {
    char tmp_file[PATH_MAX + 8];
    snprintf(tmp_file, sizeof(tmp_file), "%s%s", state_file, TMP_STATE_SUFFIX);
    FILE *f = fopen(tmp_file, "w");
    if (!f) return;
    
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    
    fprintf(f, "SYNCMON_TIMESTAMP=\"%04d-%02d-%02d %02d:%02d:%02d\"\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    fprintf(f, "OVERALL_STATUS=\"%s\"\n", status ? status : "UNKNOWN");
    fprintf(f, "MYSQL_MASTER_STATUS=\"%s\"\n", m_status ? m_status : "UNKNOWN");
    fprintf(f, "MYSQL_SLAVE_STATUS=\"%s\"\n", s_status ? s_status : "UNKNOWN");
    fprintf(f, "MYSQL_SYNC_STATUS=\"%s\"\n", sync_status ? sync_status : "UNKNOWN");
    fprintf(f, "MYSQL_MASTER_GTID=\"%s\"\n", m_gtid ? m_gtid : "");
    fprintf(f, "MYSQL_SLAVE_GTID=\"%s\"\n", s_gtid ? s_gtid : "");
    fprintf(f, "MYSQL_CHECK_TIMESTAMP=\"%s\"\n", mysql_check_ts ? mysql_check_ts : "unknown");
    fprintf(f, "REDIS_MASTER_STATUS=\"%s\"\n", rm_status ? rm_status : "UNKNOWN");
    fprintf(f, "REDIS_SLAVE_STATUS=\"%s\"\n", rs_status ? rs_status : "UNKNOWN");
    fprintf(f, "REDIS_REPLICATION_STATUS=\"%s\"\n", r_rep_status ? r_rep_status : "UNKNOWN");
    fprintf(f, "REDIS_REPLICATION_DETAIL=\"%s\"\n", r_detail ? r_detail : "");
    fprintf(f, "REDIS_CHECK_TIMESTAMP=\"%s\"\n", redis_check_ts ? redis_check_ts : "unknown");
    fprintf(f, "SYNCMON_MESSAGE=\"%s\"\n", message ? message : "");
    
    fclose(f);
    rename(tmp_file, state_file);
}

int exec_query(const char* cmd, char* out, size_t out_size) {
    FILE* fp = popen(cmd, "r");
    if (!fp) return 0;
    if (fgets(out, out_size, fp)) trim_newline(out);
    else out[0] = '\0';
    return pclose(fp) == 0;
}

void get_mysql_gtid(const char* host, const char* port, const char* user, const char* pass, const char* var, char* out) {
    char cmd[MAX_BUFFER];
    char pass_opt[300] = {0};
    if (strlen(pass) > 0) snprintf(pass_opt, sizeof(pass_opt), "-p'%s'", pass);
    
    snprintf(cmd, sizeof(cmd), "timeout 10 mysql -h %s -P %s -u %s %s -N -B -e 'SELECT @@GLOBAL.%s;' 2>/dev/null", host, port, user, pass_opt, var);
    if (!exec_query(cmd, out, 256) || strlen(out) == 0) {
        snprintf(cmd, sizeof(cmd), "timeout 10 mysql -h %s -P %s -u %s %s -N -B -e 'SHOW GLOBAL VARIABLES LIKE \"%s\";' 2>/dev/null | awk '{print $2}'", host, port, user, pass_opt, var);
        exec_query(cmd, out, 256);
    }
}

void run_checks(Config *cfg) {
    char mysql_master_status[16] = "UNKNOWN", mysql_slave_status[16] = "UNKNOWN", mysql_sync_status[16] = "UNKNOWN";
    char mysql_master_gtid[256] = "", mysql_slave_gtid[256] = "";
    char redis_master_status[16] = "UNKNOWN", redis_slave_status[16] = "UNKNOWN", redis_rep_status[16] = "UNKNOWN";
    char redis_rep_detail[256] = "unknown", overall_msg[256] = "";
    char mysql_check_ts[64] = "unknown", redis_check_ts[64] = "unknown";
    
    if (cfg->enable_mysql_check) {
        char cmd[MAX_BUFFER];
        char pass_opt[300] = {0}; 
        if (strlen(cfg->mysql_password) > 0) snprintf(pass_opt, sizeof(pass_opt), "-p'%s'", cfg->mysql_password);
        
        snprintf(cmd, sizeof(cmd), "timeout 10 mysql -h %s -P %s -u %s %s -N -B -e 'SELECT 1;' 2>/dev/null", cfg->mysql_master_host, cfg->mysql_master_port, cfg->mysql_user, pass_opt);
        int master_ok = exec_query(cmd, mysql_master_gtid, sizeof(mysql_master_gtid));
        
        snprintf(cmd, sizeof(cmd), "timeout 10 mysql -h %s -P %s -u %s %s -N -B -e 'SELECT 1;' 2>/dev/null", cfg->mysql_slave_host, cfg->mysql_slave_port, cfg->mysql_user, pass_opt);
        int slave_ok = exec_query(cmd, mysql_slave_gtid, sizeof(mysql_slave_gtid));

        if (master_ok) {
            strcpy(mysql_master_status, "OK");
            get_mysql_gtid(cfg->mysql_master_host, cfg->mysql_master_port, cfg->mysql_user, cfg->mysql_password, "gtid_binlog_pos", mysql_master_gtid);
        } else strcpy(mysql_master_status, "ERROR");

        if (slave_ok) {
            strcpy(mysql_slave_status, "OK");
            get_mysql_gtid(cfg->mysql_slave_host, cfg->mysql_slave_port, cfg->mysql_user, cfg->mysql_password, "gtid_slave_pos", mysql_slave_gtid);
        } else strcpy(mysql_slave_status, "ERROR");

        if (master_ok && slave_ok) {
            if (strlen(mysql_master_gtid) > 0 && strcmp(mysql_master_gtid, mysql_slave_gtid) == 0) strcpy(mysql_sync_status, "OK");
            else strcpy(mysql_sync_status, "WARN");
        } else strcpy(mysql_sync_status, "ERROR");

        // Set MySQL Check Timestamp
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);
        snprintf(mysql_check_ts, sizeof(mysql_check_ts), "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    }

    if (cfg->enable_redis_check) {
        char cmd[MAX_BUFFER], out[MAX_BUFFER];
        const char *pass = cfg->redis_password;
        
        snprintf(cmd, sizeof(cmd), "redis-cli --no-auth-warning -h %s -p %s %s %s PING 2>/dev/null", cfg->redis_master_host, cfg->redis_master_port, strlen(pass) ? "-a" : "", strlen(pass) ? pass : "");
        int r_master_ok = exec_query(cmd, out, sizeof(out));
        
        snprintf(cmd, sizeof(cmd), "redis-cli --no-auth-warning -h %s -p %s %s %s PING 2>/dev/null", cfg->redis_slave_host, cfg->redis_slave_port, strlen(pass) ? "-a" : "", strlen(pass) ? pass : "");
        int r_slave_ok = exec_query(cmd, out, sizeof(out));

        strcpy(redis_master_status, r_master_ok ? "OK" : "ERROR");
        strcpy(redis_slave_status, r_slave_ok ? "OK" : "ERROR");

        if (r_slave_ok) {
            snprintf(cmd, sizeof(cmd), "redis-cli --no-auth-warning -h %s -p %s %s %s INFO replication 2>/dev/null", cfg->redis_slave_host, cfg->redis_slave_port, strlen(pass) ? "-a" : "", strlen(pass) ? pass : "");
            FILE* fp = popen(cmd, "r");
            if (fp) {
                char line[256], link[16] = "?", io[16] = "?", host[64] = "?";
                int rep_ok = 0;
                while(fgets(line, sizeof(line), fp)) {
                    if (strncmp(line, "master_link_status:", 19) == 0) { sscanf(line+19, "%s", link); if(strcmp(link,"up")==0) rep_ok = 1; }
                    else if (strncmp(line, "master_last_io_seconds_ago:", 27) == 0) sscanf(line+27, "%s", io);
                    else if (strncmp(line, "master_host:", 12) == 0) sscanf(line+12, "%s", host);
                }
                pclose(fp);
                snprintf(redis_rep_detail, sizeof(redis_rep_detail), "link=%s io=%s host=%s", link, io, host);
                strcpy(redis_rep_status, rep_ok ? "OK" : "WARN");
            }
        } else strcpy(redis_rep_status, "ERROR");

        // Set Redis Check Timestamp
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);
        snprintf(redis_check_ts, sizeof(redis_check_ts), "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    }

    char overall[16];
    if (strcmp(mysql_master_status, "OK") == 0 && strcmp(mysql_slave_status, "OK") == 0 && strcmp(mysql_sync_status, "OK") == 0 &&
        strcmp(redis_master_status, "OK") == 0 && strcmp(redis_slave_status, "OK") == 0 && strcmp(redis_rep_status, "OK") == 0) {
        strcpy(overall, "OK"); strcpy(overall_msg, "All systems operational");
    } else if (strcmp(mysql_master_status, "ERROR") == 0 && strcmp(redis_master_status, "ERROR") == 0) {
        strcpy(overall, "ERROR"); strcpy(overall_msg, "Multiple master failures detected");
    } else {
        strcpy(overall, "WARN"); strcpy(overall_msg, "One or more components degraded");
    }

    write_state(cfg->state_file, overall, mysql_master_status, mysql_slave_status, mysql_sync_status, mysql_master_gtid, mysql_slave_gtid, redis_master_status, redis_slave_status, redis_rep_status, redis_rep_detail, overall_msg, mysql_check_ts, redis_check_ts);
    
    char log_buf[512];
    snprintf(log_buf, sizeof(log_buf), "check complete: %s", overall);
    log_message(cfg, log_buf);
}

void run_test_mode(const char* state_file) {
    srand(time(NULL));
    const char* statuses[] = {"OK", "WARN", "ERROR"};
    const char* base_gtid = (rand() % 2) ? "3E111111-1111-1111-1111-111111111111" : "4F222222-2222-2222-2222-222222222222";
    int m_gtid = (rand() % 10000) + 1000, s_gtid = m_gtid;
    if ((rand() % 100) < 30) s_gtid -= (rand() % 50 + 1);

    char m_gtid_str[256], s_gtid_str[256], r_detail[256], ts_str[64];
    snprintf(m_gtid_str, sizeof(m_gtid_str), "%s:%d", base_gtid, m_gtid);
    snprintf(s_gtid_str, sizeof(s_gtid_str), "%s:%d", base_gtid, s_gtid);
    
    const char* rep_status = statuses[rand() % 3];
    snprintf(r_detail, sizeof(r_detail), "link=%s io=%d host=172.31.%d.%d", strcmp(rep_status, "ERROR") == 0 ? "down" : "up", rand() % 15, rand() % 255, rand() % 255);
    
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    snprintf(ts_str, sizeof(ts_str), "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

    write_state(state_file, statuses[rand() % 3], statuses[rand() % 3], statuses[rand() % 3], m_gtid == s_gtid ? "OK" : "WARN", m_gtid_str, s_gtid_str, statuses[rand() % 3], statuses[rand() % 3], rep_status, r_detail, "Live Simulation Data", ts_str, ts_str);
    printf("Simulated test state written to %s\n", state_file);
    exit(0);
}

int main(int argc, char *argv[]) {
    Config cfg;
    init_config(&cfg);
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test") == 0) run_test_mode(cfg.state_file);
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: syncmon-daemon [--test] [--help]\n\n--test Write a sample syncmon state file and exit\n--help Show this usage message\n");
            return 0;
        } else {
            fprintf(stderr, "ERROR: unknown argument: %s\n", argv[i]);
            return 1;
        }
    }

    char exe_path[PATH_MAX], config_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        snprintf(config_path, sizeof(config_path), "%s/ressources/syncmon.conf", dirname(exe_path));
    } else strncpy(config_path, "ressources/syncmon.conf", PATH_MAX);

    load_config(config_path, &cfg);

    if (cfg.startup_check) {
        log_message(&cfg, "Performing initial startup checks...");
        run_checks(&cfg);
    }

    char startup_msg[PATH_MAX + 128];
    snprintf(startup_msg, sizeof(startup_msg), "SyncMon daemon starting (interval=%ds, state=%s)", cfg.check_interval, cfg.state_file);
    log_message(&cfg, startup_msg);

    while (1) {
        run_checks(&cfg);
        sleep(cfg.check_interval);
    }
    return 0;
}

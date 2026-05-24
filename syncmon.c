/*
 * syncmon.c  ─  Nextcloud HA Cluster Monitor
 * Visual overhaul: animated headers, rich Unicode symbols, glow status badges,
 * AI analysis panel, improved layout, sparkline graphs, rich footer.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
#define TB_IMPL
#include "termbox2.h"
#pragma GCC diagnostic pop
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <math.h>

#define MAX_LINE  1024
#define MAX_VAL   256
#define HIST_SIZE 600

#define STATE_FILE_DEFAULT "/var/log/syncmon/syncmon_state.env"

/* ── Unicode symbol constants ─────────────────────────────────────────── */
#define SYM_OK       "\u2714"   /* ✔  heavy check      */
#define SYM_WARN     "\u26A0"   /* ⚠  warning triangle */
#define SYM_ERR      "\u2718"   /* ✘  heavy cross      */
#define SYM_SKIP     "\u25CB"   /* ○  empty circle     */
#define SYM_BULLET   "\u25CF"   /* ●  filled circle    */
#define SYM_ARROW    "\u27A4"   /* ➤  right arrow      */
#define SYM_DB       "\u25BC"   /* ▼  database         */
#define SYM_REDIS    "\u25B6"   /* ▶  redis            */
#define SYM_LB       "\u2248"   /* ≈  loadbalancer     */
#define SYM_DNS      "\u2318"   /* ⌘  dns              */
#define SYM_NC       "\u2601"   /* ☁  cloud            */
#define SYM_NFS      "\u25A0"   /* ■  storage          */
#define SYM_HISTORY  "\u29D7"   /* ⧗  hourglass        */
#define SYM_AI       "\u2736"   /* ✶  sparkle/AI       */
#define SYM_PULSE    "\u25C9"   /* ◉  target           */
#define SYM_LINK     "\u21C4"   /* ⇄  sync arrows      */
#define SYM_MASTER   "\u2605"   /* ★  master star      */
#define SYM_SLAVE    "\u2606"   /* ☆  slave star       */
#define SYM_CLOCK    "\u29D6"   /* ⧖  clock            */
#define SYM_HOST     "\u25BA"   /* ►  host             */

/* ── Spinner sets ─────────────────────────────────────────────────────── */
static const char* SPIN_BRAILLE[]  = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
static const char* SPIN_DOTS[]     = {"⣾","⣽","⣻","⢿","⡿","⣟","⣯","⣷","⣾","⣽"};
static const char* SPIN_PULSE[]    = {"◉","◎","○","◎","◉","●","◉","◎","○","◎"};
static const char* SPIN_ARROW[]    = {"←","↖","↑","↗","→","↘","↓","↙","←","↖"};

/* ── Block fill chars for progress bars ────────────────────────────────── */
static const uint32_t BLOCKS[]     = {0x2581,0x2582,0x2583,0x2584,0x2585,0x2586,0x2587,0x2588};
#define BAR_FULL  0x2588  /* █ */
#define BAR_MED   0x2593  /* ▓ */
#define BAR_LOW   0x2591  /* ░ */
#define BAR_HALF  0x2584  /* ▄ */

/* ── Theme definition ───────────────────────────────────────────────────── */
typedef struct {
    const char* name;
    uint16_t bg, fg, box1, box2, highlight, accent, ok, warn, err, skip;
    uint16_t hdr_bg, hdr_fg;   /* header row colors */
    uint16_t badge_bg;         /* status badge bg   */
} Theme;

Theme themes[] = {
    /* name           bg   fg   box1 box2  hi    acc   ok   warn err  skip  hbg  hfg  bbg */
    { "Default",      TB_DEFAULT,TB_DEFAULT,
                                13,  12,  11|TB_BOLD, 14|TB_BOLD,
                                46|TB_BOLD, 226|TB_BOLD, 196|TB_BOLD, 244,
                                237, 14,   238 },
    { "Monokai",      235, 252, 197,  81, 148, 208, 148, 208, 197,  81,  234, 208, 237 },
    { "Dracula",      236, 253, 212, 117, 205, 228,  84, 228, 203, 117,  234, 212, 238 },
    { "Nord",         237, 231, 109, 110, 114, 110, 114, 220, 174, 109,  235, 110, 239 },
    { "Gruvbox",      235, 223, 208, 109, 214, 175, 142, 214, 167, 109,  234, 214, 237 },
    { "Cyberpunk",    233, 253, 199,  51, 226,  39,  46, 226, 196, 240,  232, 201, 235 },
    { "Calm",         235, 252,  67,  66, 109, 151, 108, 179, 167, 242,  234,  67, 237 },
    { "White Paper",  230, 236, 240, 242,  22,  88,  28, 130, 124, 246,  231,  22, 252 },
    { "Grayscale",    233, 250, 240, 244, 255, 252, 246, 250, 255|TB_BOLD, 238, 234, 255, 237 },
    { "Ocean",        234, 195,  39,  45,  51,  81,  46, 220, 196, 240,  232,  45, 236 },
    { "Lava",         233, 223, 202,  214,220, 196,  46, 214, 196, 244,  232, 202, 235 },
};

int num_themes    = sizeof(themes) / sizeof(Theme);
int current_theme = 0;
int theme_menu_open = 0;
int theme_menu_sel  = 0;

/* ── Config ───────────────────────────────────────────────────────────── */
struct {
    int  display_refresh;
    int  test_mode;
    char state_file[MAX_VAL];
    int  dash_w;
    int  panel_l;
    int  panel_r;
    int  use_braille;
    int  spinner_style;  /* 0=braille 1=dots 2=pulse 3=arrow */
    int  show_ai;        /* show AI analysis panel */
} config = {
    .display_refresh = 2,
    .test_mode       = 0,
    .state_file      = STATE_FILE_DEFAULT,
    .dash_w          = 92,
    .panel_l         = 42,
    .panel_r         = 48,
    .use_braille     = 1,
    .spinner_style   = 0,
    .show_ai         = 1,
};

/* ── State ────────────────────────────────────────────────────────────── */
struct {
    char overall_status[MAX_VAL];
    char timestamp[MAX_VAL];
    char message[MAX_VAL];
    char m_m_status[MAX_VAL];  char m_s_status[MAX_VAL];
    char m_sync[MAX_VAL];      char m_m_gtid[MAX_VAL];  char m_s_gtid[MAX_VAL];
    char m_chk[MAX_VAL];       char m_m_ep[MAX_VAL];    char m_s_ep[MAX_VAL];
    char r_m_status[MAX_VAL];  char r_s_status[MAX_VAL];
    char r_sync[MAX_VAL];      char r_det[MAX_VAL];     char r_chk[MAX_VAL];
    char r_m_ep[MAX_VAL];      char r_s_ep[MAX_VAL];
    char lb_host[MAX_VAL];     char lb_ping[MAX_VAL];   char lb_ping_status[MAX_VAL];
    char lb_check[MAX_VAL];    char lb_chk[MAX_VAL];
    char dns_host[MAX_VAL];    char dns_ping[MAX_VAL];  char dns_ping_status[MAX_VAL];
    char dns_check[MAX_VAL];   char dns_chk[MAX_VAL];
    char nc1_host[MAX_VAL];    char nc1_ping[MAX_VAL];  char nc1_ping_status[MAX_VAL];
    char nc1_check[MAX_VAL];   char nc1_chk[MAX_VAL];
    char nc2_host[MAX_VAL];    char nc2_ping[MAX_VAL];  char nc2_ping_status[MAX_VAL];
    char nc2_check[MAX_VAL];   char nc2_chk[MAX_VAL];
    char nfs_host[MAX_VAL];    char nfs_ping[MAX_VAL];  char nfs_ping_status[MAX_VAL];
    char nfs_check[MAX_VAL];   char nfs_chk[MAX_VAL];
} state;

int keep_running = 1;
int hist_mysql[HIST_SIZE];
int hist_redis[HIST_SIZE];
int hist_idx = 0;

/* ── AI analysis cache ─────────────────────────────────────────────────── */
static char ai_line1[MAX_VAL] = "";
static char ai_line2[MAX_VAL] = "";
static char ai_line3[MAX_VAL] = "";

void handle_sig(int sig) { (void)sig; keep_running = 0; }

uint64_t get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

void get_env_or(const char* key, const char* def, char* out, size_t sz) {
    const char* val = getenv(key);
    if (val && strlen(val) == 0) val = NULL;
    const char* fv = val ? val : def;
    if (fv != out) snprintf(out, sz, "%s", fv);
}

void parse_env_file(const char* filepath) {
    FILE* f = fopen(filepath, "r");
    if (!f) return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            char* key = line;
            char* val = eq + 1;
            val[strcspn(val, "\r\n")] = 0;
            if ((val[0] == '"' || val[0] == '\'') && val[strlen(val)-1] == val[0]) {
                val[strlen(val)-1] = '\0'; val++;
            }
            setenv(key, val, 1);
        }
    }
    fclose(f);
}

int rand_range(int min, int max) { return min + rand() % (max - min + 1); }
const char* rand_status()        { const char* s[] = {"OK","WARN","ERROR"}; return s[rand()%3]; }

int status_to_val(const char* s) {
    if (strcmp(s,"OK")==0)    return 4;
    if (strcmp(s,"WARN")==0)  return 2;
    if (strcmp(s,"ERROR")==0) return 0;
    return -1;
}

static void derive_check_status(const char* check, char* out, size_t sz) {
    if (!check || strlen(check) == 0 || strcmp(check, "N/A") == 0) {
        snprintf(out, sz, "N/A"); return;
    }
    if (strncmp(check, "ERROR", 5) == 0)                         { snprintf(out,sz,"ERROR"); return; }
    if (strstr(check, "unreachable"))                             { snprintf(out,sz,"ERROR"); return; }
    if (strstr(check, "timeout"))                                 { snprintf(out,sz,"ERROR"); return; }
    if (strstr(check, "NXDOMAIN"))                                { snprintf(out,sz,"ERROR"); return; }
    if (strstr(check, " 503") || strstr(check, " 502") ||
        strstr(check, " 500") || strstr(check, " 504"))          { snprintf(out,sz,"ERROR"); return; }
    if (strstr(check, "link=down"))                               { snprintf(out,sz,"ERROR"); return; }
    if (strstr(check, "no exports"))                              { snprintf(out,sz,"ERROR"); return; }
    if (strstr(check, "WARN") || strstr(check, "WARNING"))       { snprintf(out,sz,"WARN");  return; }
    if (strstr(check, "maintenance=true"))                        { snprintf(out,sz,"WARN");  return; }
    if (strstr(check, " 4") && strstr(check, "HTTP"))            { snprintf(out,sz,"WARN");  return; }
    {
        const char *bp = strstr(check, "backend ");
        if (bp) {
            int avail = 0, total = 0;
            if (sscanf(bp + 8, "%d/%d", &avail, &total) == 2 && total > 0 && avail < total)
                { snprintf(out,sz,"WARN"); return; }
        }
    }
    snprintf(out, sz, "OK");
}

/* ── AI analysis generator ─────────────────────────────────────────────── */
static void compute_ai_analysis() {
    int errors = 0, warns = 0;
    const char* checks[] = {
        state.overall_status, state.m_m_status, state.m_s_status,
        state.m_sync, state.r_m_status, state.r_s_status, state.r_sync,
        state.lb_ping_status, state.dns_ping_status,
        state.nc1_ping_status, state.nc2_ping_status, state.nfs_ping_status,
        NULL
    };
    for (int i = 0; checks[i]; i++) {
        if (strcmp(checks[i],"ERROR")==0) errors++;
        else if (strcmp(checks[i],"WARN")==0) warns++;
    }

    /* line 1: overall health score */
    int total = 11;
    int healthy = total - errors - warns;
    int pct = (healthy * 100) / total;
    if (errors == 0 && warns == 0)
        snprintf(ai_line1, sizeof(ai_line1), "All systems nominal — cluster health 100%%");
    else if (errors == 0)
        snprintf(ai_line1, sizeof(ai_line1), "Health %d%% — %d warning(s) detected, no critical faults", pct, warns);
    else
        snprintf(ai_line1, sizeof(ai_line1), "Health %d%% — %d error(s), %d warning(s) require attention", pct, errors, warns);

    /* line 2: MariaDB/Redis focus */
    int db_ok  = (strcmp(state.m_sync,"OK")==0);
    int red_ok = (strcmp(state.r_sync,"OK")==0);
    if (db_ok && red_ok)
        snprintf(ai_line2, sizeof(ai_line2), "Replication: MariaDB GTID in sync, Redis replication stable");
    else if (!db_ok && !red_ok)
        snprintf(ai_line2, sizeof(ai_line2), "WARNING: Both MariaDB GTID and Redis replication show issues");
    else if (!db_ok)
        snprintf(ai_line2, sizeof(ai_line2), "MariaDB GTID drift detected — check master/slave GTID delta");
    else
        snprintf(ai_line2, sizeof(ai_line2), "Redis replication degraded — inspect replication link status");

    /* line 3: infrastructure hint */
    int nc_ok  = (strcmp(state.nc1_ping_status,"OK")==0 && strcmp(state.nc2_ping_status,"OK")==0);
    int nfs_ok = (strcmp(state.nfs_ping_status,"OK")==0);
    int lb_ok  = (strcmp(state.lb_ping_status,"OK")==0);
    if (nc_ok && nfs_ok && lb_ok)
        snprintf(ai_line3, sizeof(ai_line3), "Nextcloud, NFS storage, and load balancer all reachable");
    else {
        char parts[MAX_VAL] = "";
        if (!lb_ok)  strncat(parts, "LoadBalancer ", sizeof(parts)-strlen(parts)-1);
        if (!nc_ok)  strncat(parts, "Nextcloud ",    sizeof(parts)-strlen(parts)-1);
        if (!nfs_ok) strncat(parts, "NFS ",          sizeof(parts)-strlen(parts)-1);
        snprintf(ai_line3, sizeof(ai_line3), "Unreachable nodes detected: %s— investigate connectivity", parts);
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 *  STATE LOADING
 * ───────────────────────────────────────────────────────────────────────── */
void load_state() {
    time_t t = time(NULL); struct tm tm = *localtime(&t);
    char now[64]; strftime(now, sizeof(now), "%Y-%m-%d %H:%M:%S", &tm);

    if (config.test_mode) {
        const char* p[] = {"3E111111-1111-1111-1111-111111111111","4F222222-2222-2222-2222-222222222222"};
        const char* base = p[rand()%2];
        int mg = rand_range(1000,10999), sg = mg;
        if (rand_range(0,99)<30) sg = mg - rand_range(1,50);
        snprintf(state.m_m_gtid, sizeof(state.m_m_gtid), "%s:%d", base, mg);
        snprintf(state.m_s_gtid, sizeof(state.m_s_gtid), "%s:%d", base, sg);
        strcpy(state.m_sync,        mg!=sg ? "WARN" : "OK");
        strcpy(state.overall_status, rand_status());
        strcpy(state.m_m_status,    rand_status()); strcpy(state.m_s_status, rand_status());
        strcpy(state.r_m_status,    rand_status()); strcpy(state.r_s_status, rand_status());
        strcpy(state.r_sync,        rand_status());
        const char* rl = strcmp(state.r_sync,"ERROR")==0 ? "down" : "up";
        snprintf(state.r_det, sizeof(state.r_det), "link=%s io=%d host=172.31.%d.%d",
                 rl, rand_range(0,14), rand_range(0,254), rand_range(0,254));
        strcpy(state.timestamp, now); strcpy(state.m_chk, now); strcpy(state.r_chk, now);
        strcpy(state.m_m_ep,  "172.31.49.233:3306"); strcpy(state.m_s_ep,  "172.31.40.234:3306");
        strcpy(state.r_m_ep,  "172.31.40.234:6379"); strcpy(state.r_s_ep,  "172.31.40.233:6379");
        strcpy(state.message, "Live Simulation Data Active");

        const char* ping_labels[] = {"OK  3ms", "OK  12ms", "WARN  251ms", "ERROR  timeout"};
        const char* ping_st[]     = {"OK",       "OK",       "WARN",        "ERROR"};
#define MOCK_PING(pfx) \
        { int r=rand()%4; \
          strcpy(state.pfx##_ping, ping_labels[r]); \
          strcpy(state.pfx##_ping_status, ping_st[r]); }

        MOCK_PING(lb)
        strcpy(state.lb_host,  "172.31.0.10");
        strcpy(state.lb_check, rand()%4 ? "HAProxy active, backend 3/3 up" : "WARNING backend 2/3 up");
        strcpy(state.lb_chk,   now);
        MOCK_PING(dns)
        strcpy(state.dns_host,  "172.31.0.53");
        strcpy(state.dns_check, rand()%4 ? "resolv OK: cluster.local" : "ERROR query timeout");
        strcpy(state.dns_chk,   now);
        MOCK_PING(nc1)
        strcpy(state.nc1_host,  "172.31.1.11");
        strcpy(state.nc1_check, rand()%4 ? "HTTP 200 /status: maintenance=false" : "HTTP 503 /status");
        strcpy(state.nc1_chk,   now);
        MOCK_PING(nc2)
        strcpy(state.nc2_host,  "172.31.1.12");
        strcpy(state.nc2_check, rand()%4 ? "HTTP 200 /status: maintenance=false" : "HTTP 503 /status");
        strcpy(state.nc2_chk,   now);
        MOCK_PING(nfs)
        strcpy(state.nfs_host,  "172.31.2.20");
        strcpy(state.nfs_check, rand()%4 ? "mount OK, rw, 1.2T free" : "ERROR mount unreachable");
        strcpy(state.nfs_chk,   now);
#undef MOCK_PING

    } else {
        if (access(config.state_file, R_OK)==0) parse_env_file(config.state_file);
        get_env_or("OVERALL_STATUS",         "NO DATA",              state.overall_status, sizeof(state.overall_status));
        get_env_or("MYSQL_MASTER_STATUS",     "N/A",                  state.m_m_status,     sizeof(state.m_m_status));
        get_env_or("MYSQL_SLAVE_STATUS",      "N/A",                  state.m_s_status,     sizeof(state.m_s_status));
        get_env_or("MYSQL_SYNC_STATUS",       "N/A",                  state.m_sync,         sizeof(state.m_sync));
        get_env_or("MYSQL_MASTER_GTID",       "unknown",              state.m_m_gtid,       sizeof(state.m_m_gtid));
        get_env_or("MYSQL_SLAVE_GTID",        "unknown",              state.m_s_gtid,       sizeof(state.m_s_gtid));
        get_env_or("REDIS_MASTER_STATUS",     "N/A",                  state.r_m_status,     sizeof(state.r_m_status));
        get_env_or("REDIS_SLAVE_STATUS",      "N/A",                  state.r_s_status,     sizeof(state.r_s_status));
        get_env_or("REDIS_REPLICATION_STATUS","N/A",                  state.r_sync,         sizeof(state.r_sync));
        get_env_or("REDIS_REPLICATION_DETAIL","not available",        state.r_det,          sizeof(state.r_det));
        get_env_or("SYNCMON_MESSAGE",         "Waiting for daemon...",state.message,        sizeof(state.message));
        get_env_or("SYNCMON_TIMESTAMP",       now,                    state.timestamp,      sizeof(state.timestamp));
        get_env_or("MYSQL_CHECK_TIMESTAMP",   state.timestamp,        state.m_chk,          sizeof(state.m_chk));
        get_env_or("REDIS_CHECK_TIMESTAMP",   state.timestamp,        state.r_chk,          sizeof(state.r_chk));
        char mh[64],mp[64],sh[64],sp[64];
        get_env_or("MYSQL_MASTER_HOST","unknown",mh,sizeof(mh)); get_env_or("MYSQL_MASTER_PORT","?",mp,sizeof(mp));
        snprintf(state.m_m_ep, sizeof(state.m_m_ep), "%s:%s", mh, mp);
        get_env_or("MYSQL_SLAVE_HOST", "unknown",sh,sizeof(sh)); get_env_or("MYSQL_SLAVE_PORT", "?",sp,sizeof(sp));
        snprintf(state.m_s_ep, sizeof(state.m_s_ep), "%s:%s", sh, sp);
        get_env_or("REDIS_MASTER_HOST","unknown",mh,sizeof(mh)); get_env_or("REDIS_MASTER_PORT","?",mp,sizeof(mp));
        snprintf(state.r_m_ep, sizeof(state.r_m_ep), "%s:%s", mh, mp);
        get_env_or("REDIS_SLAVE_HOST", "unknown",sh,sizeof(sh)); get_env_or("REDIS_SLAVE_PORT", "?",sp,sizeof(sp));
        snprintf(state.r_s_ep, sizeof(state.r_s_ep), "%s:%s", sh, sp);
#define LOAD_COMP(pfx, KEY) \
        get_env_or(KEY"_HOST",            "N/A",   state.pfx##_host,        sizeof(state.pfx##_host)); \
        get_env_or(KEY"_PING",            "N/A",   state.pfx##_ping,        sizeof(state.pfx##_ping)); \
        get_env_or(KEY"_PING_STATUS",     "N/A",   state.pfx##_ping_status, sizeof(state.pfx##_ping_status)); \
        get_env_or(KEY"_CHECK",           "N/A",   state.pfx##_check,       sizeof(state.pfx##_check)); \
        get_env_or(KEY"_CHECK_TIMESTAMP", "never", state.pfx##_chk,         sizeof(state.pfx##_chk));
        LOAD_COMP(lb,  "LB")
        LOAD_COMP(dns, "DNS")
        LOAD_COMP(nc1, "NC1")
        LOAD_COMP(nc2, "NC2")
        LOAD_COMP(nfs, "NFS")
#undef LOAD_COMP
    }
    hist_mysql[hist_idx] = status_to_val(state.m_sync);
    hist_redis[hist_idx] = status_to_val(state.r_sync);
    hist_idx = (hist_idx + 1) % HIST_SIZE;
    compute_ai_analysis();
}

/* ─────────────────────────────────────────────────────────────────────────
 *  UI HELPERS
 * ───────────────────────────────────────────────────────────────────────── */
int tb_print_custom(int x, int y, uint16_t fg, uint16_t bg, const char *str) {
    while (*str) { uint32_t u; str += tb_utf8_char_to_unicode(&u, str); tb_set_cell(x++,y,u,fg,bg); }
    return 0;
}

void tb_print_fixed(int x, int y, uint16_t fg, uint16_t bg, const char *str, int w) {
    int p=0;
    while (*str && p<w) { uint32_t u; str+=tb_utf8_char_to_unicode(&u,str); tb_set_cell(x++,y,u,fg,bg); p++; }
    while (p++<w) tb_set_cell(x++,y,' ',fg,bg);
}

/* center-print a string in a field of width w */
void tb_print_center(int x, int y, uint16_t fg, uint16_t bg, const char *str, int w) {
    int len = 0;
    const char* p = str;
    while (*p) { uint32_t u; p += tb_utf8_char_to_unicode(&u, p); len++; }
    int pad = (w - len) / 2;
    if (pad < 0) pad = 0;
    for (int i = 0; i < pad; i++) tb_set_cell(x+i, y, ' ', fg, bg);
    tb_print_fixed(x+pad, y, fg, bg, str, w-pad);
}

/* fill a horizontal span with a single character */
void tb_hline(int x, int y, uint32_t ch, uint16_t fg, uint16_t bg, int len) {
    for (int i = 0; i < len; i++) tb_set_cell(x+i, y, ch, fg, bg);
}

/* fill a rectangle */
void tb_fill(int x, int y, int w, int h, uint16_t fg, uint16_t bg) {
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            tb_set_cell(x+col, y+row, ' ', fg, bg);
}

uint16_t get_status_fg(const char* s, Theme* th) {
    if (strcmp(s,"OK")==0)    return th->ok;
    if (strcmp(s,"WARN")==0)  return th->warn;
    if (strcmp(s,"ERROR")==0) return th->err;
    return th->skip;
}

/* ── Status badge: " ✔ OK " / " ⚠ WARN " / " ✘ ERROR " ────────────────── */
void draw_badge(int x, int y, const char* s, Theme* th) {
    uint16_t fg = get_status_fg(s, th);
    uint16_t bg = th->badge_bg;
    const char* sym;
    const char* label;
    int w;
    if      (strcmp(s,"OK")==0)    { sym=SYM_OK;   label="OK   "; w=7; }
    else if (strcmp(s,"WARN")==0)  { sym=SYM_WARN; label="WARN "; w=7; }
    else if (strcmp(s,"ERROR")==0) { sym=SYM_ERR;  label="ERROR"; w=7; }
    else                           { sym=SYM_SKIP; label=s;       w=7; }

    tb_set_cell(x,   y, ' ',  fg, bg);
    tb_print_custom(x+1, y, fg|TB_BOLD, bg, sym);
    tb_set_cell(x+2, y, ' ',  fg, bg);
    tb_print_fixed(x+3, y, fg|TB_BOLD, bg, label, w-3);
    tb_set_cell(x+w, y, ' ',  fg, bg);
}

/* ── Thin progress bar with gradient fill ───────────────────────────────── */
void draw_glow_bar(int x, int y, const char* s, Theme* th, int bar_w) {
    int fill = 0;
    if      (strcmp(s,"OK")==0)    fill = bar_w;
    else if (strcmp(s,"WARN")==0)  fill = (bar_w * 5) / 8;
    else if (strcmp(s,"ERROR")==0) fill = (bar_w * 2) / 8;
    else if (strcmp(s,"N/A")==0)   fill = 1;
    uint16_t fg = get_status_fg(s, th);
    for (int i = 0; i < bar_w; i++) {
        uint32_t ch = (i < fill) ? BAR_FULL : BAR_LOW;
        uint16_t c  = (i < fill) ? fg : th->skip;
        tb_set_cell(x+i, y, ch, c, th->bg);
    }
}

/* ── Animated double-line box with rounded corners ──────────────────────── */
void draw_box(int x, int y, int w, int h, uint16_t fg, const char* title, Theme* th) {
    /* corners: ╭ ╮ ╰ ╯  (rounded) */
    tb_set_cell(x,     y,     0x256D, fg, th->bg);
    tb_set_cell(x+w-1, y,     0x256E, fg, th->bg);
    tb_set_cell(x,     y+h-1, 0x2570, fg, th->bg);
    tb_set_cell(x+w-1, y+h-1, 0x256F, fg, th->bg);
    for (int i=1;i<w-1;i++) {
        tb_set_cell(x+i, y,     0x2500, fg, th->bg);
        tb_set_cell(x+i, y+h-1, 0x2500, fg, th->bg);
    }
    for (int i=1;i<h-1;i++) {
        tb_set_cell(x,     y+i, 0x2502, fg, th->bg);
        tb_set_cell(x+w-1, y+i, 0x2502, fg, th->bg);
    }
    if (title) {
        int tl = 0;
        const char* tp = title;
        while (*tp) { uint32_t u; tp += tb_utf8_char_to_unicode(&u, tp); tl++; }
        int tx = x + (w - tl - 4) / 2;
        if (tx < x+1) tx = x+1;
        /* " ◈ TITLE ◈ " style title bar */
        tb_set_cell(tx,       y, ' ',   th->bg,      th->bg);
        tb_set_cell(tx+1,     y, 0x25C8,th->accent,  th->bg); /* ◈ */
        tb_set_cell(tx+2,     y, ' ',   th->bg,      th->bg);
        tb_print_custom(tx+3, y, th->hdr_fg|TB_BOLD, th->bg, title);
        tb_set_cell(tx+3+tl,  y, ' ',   th->bg,      th->bg);
        tb_set_cell(tx+3+tl+1,y, 0x25C8,th->accent,  th->bg);
        tb_set_cell(tx+3+tl+2,y, ' ',   th->bg,      th->bg);
    }
}

/* ── Thick accent box (for header) ─────────────────────────────────────── */
void draw_header_box(int x, int y, int w, int h, Theme* th) {
    /* ╔═╗  ║  ╚═╝ */
    tb_set_cell(x,     y,     0x2554, th->box1, th->hdr_bg);
    tb_set_cell(x+w-1, y,     0x2557, th->box1, th->hdr_bg);
    tb_set_cell(x,     y+h-1, 0x255A, th->box1, th->hdr_bg);
    tb_set_cell(x+w-1, y+h-1, 0x255D, th->box1, th->hdr_bg);
    for (int i=1;i<w-1;i++) {
        tb_set_cell(x+i, y,     0x2550, th->box1, th->hdr_bg);
        tb_set_cell(x+i, y+h-1, 0x2550, th->box1, th->hdr_bg);
    }
    for (int i=1;i<h-1;i++) {
        tb_fill(x, y+i, w, 1, th->hdr_fg, th->hdr_bg);
        tb_set_cell(x,     y+i, 0x2551, th->box1, th->hdr_bg);
        tb_set_cell(x+w-1, y+i, 0x2551, th->box1, th->hdr_bg);
    }
}

/* ── Braille sparkline graph ────────────────────────────────────────────── */
uint32_t braille_bar(int h1, int h2) {
    if(h1<0)h1=0;if(h1>4)h1=4;if(h2<0)h2=0;if(h2>4)h2=4;
    uint32_t b=0x2800;
    if(h1>=1)b|=0x40;if(h1>=2)b|=0x04;if(h1>=3)b|=0x02;if(h1>=4)b|=0x01;
    if(h2>=1)b|=0x80;if(h2>=2)b|=0x20;if(h2>=3)b|=0x10;if(h2>=4)b|=0x08;
    return b;
}

void draw_graph(int x, int y, int* hd, int head, int mw, Theme* th) {
    for (int c=0;c<mw;c++) {
        int idx=head-1-(mw-1-c); while(idx<0)idx+=HIST_SIZE; idx%=HIST_SIZE;
        int v=hd[idx];
        if (v==-1) { tb_set_cell(x+c,y,0x2508,th->skip,th->bg); continue; } /* ┈ dotted */
        if (v==0)  { tb_set_cell(x+c,y,0x2588,th->err|TB_BOLD,th->bg); continue; }
        uint16_t col=(v==2)?th->warn:th->ok;
        if (config.use_braille) tb_set_cell(x+c,y,braille_bar(v,v),col,th->bg);
        else { uint32_t ch=BLOCKS[(v*2)-1 < 7 ? (v*2)-1 : 7]; tb_set_cell(x+c,y,ch,col,th->bg); }
    }
}

/* ── Theme selection menu ───────────────────────────────────────────────── */
void draw_theme_menu(Theme* th) {
    if (!theme_menu_open) return;
    int w=36, h=num_themes+4, sx=(tb_width()-w)/2, sy=(tb_height()-h)/2;
    tb_fill(sx, sy, w, h, th->fg, th->bg);
    draw_box(sx, sy, w, h, th->accent, "Select Theme", th);
    tb_print_center(sx+1, sy+1, th->accent, th->bg, "↑/↓ navigate  Enter select  Esc cancel", w-2);
    tb_hline(sx+1, sy+2, 0x2508, th->skip, th->bg, w-2);
    for (int i=0;i<num_themes;i++) {
        uint16_t ibg = (i==theme_menu_sel) ? th->accent : th->bg;
        uint16_t ifg = (i==theme_menu_sel) ? th->bg     : th->fg;
        tb_fill(sx+2, sy+3+i, w-4, 1, ifg, ibg);
        tb_print_fixed(sx+3, sy+3+i, ifg|(i==theme_menu_sel?TB_BOLD:0), ibg, themes[i].name, w-6);
        if (i==theme_menu_sel)
            tb_print_custom(sx+w-5, sy+3+i, th->bg|TB_BOLD, ibg, " ◀ ");
    }
}

void format_shortened_left(char *buf, size_t bsz, const char* prefix, const char* val, int maxlen) {
    int pl=strlen(prefix), vl=strlen(val);
    if(pl+vl<=maxlen) snprintf(buf,bsz,"%s%s",prefix,val);
    else { int kl=maxlen-pl-3; if(kl<0) snprintf(buf,bsz,"%.*s",maxlen,prefix); else snprintf(buf,bsz,"%s...%s",prefix,val+(vl-kl)); }
}

/* ── Inline spinner ─────────────────────────────────────────────────────── */
const char* get_spinner(int tick) {
    switch (config.spinner_style) {
        case 1: return SPIN_DOTS[tick%10];
        case 2: return SPIN_PULSE[tick%10];
        case 3: return SPIN_ARROW[tick%8];
        default:return SPIN_BRAILLE[tick%10];
    }
}

/* ── draw_row_label: icon + label, returns end x ───────────────────────── */
int draw_row_label(int x, int y, uint16_t fg, uint16_t bg, const char* icon, const char* label, Theme* th) {
    tb_print_custom(x, y, th->accent, bg, icon);
    tb_set_cell(x+1, y, ' ', fg, bg);
    tb_print_custom(x+2, y, fg, bg, label);
    int llen = strlen(label);
    return x + 2 + llen;
}

/* ─────────────────────────────────────────────────────────────────────────
 *  PANEL DRAW FUNCTIONS
 * ───────────────────────────────────────────────────────────────────────── */

/*
 * draw_simple_panel  ─  network nodes (LB, DNS, NC1/2, NFS)
 *
 *  ╭─ ◈ TITLE ◈ ──────────────────────╮
 *  │ ► Ping   :  ✔ OK    ███████░░░  3ms  │
 *  │ ◉ Host   :  172.31.x.x            │
 *  │ ≈ Check  :  ✔ OK    ███████░░░  detail │
 *  │ ⧖ Checked:  2026-05-24 14:18:00   │
 *  ╰────────────────────────────────────╯
 */
void draw_simple_panel(int x, int y, int w, int h,
                       const char* title,
                       const char* host,
                       const char* ping_status,
                       const char* ping,
                       const char* check,
                       const char* chk_ts,
                       Theme* th)
{
    draw_box(x, y, w, h, th->box2, title, th);

    char buf[MAX_VAL];
    int col = x + 2;
    int label_end = col + 12; /* fixed label column */

    /* row 1: ping */
    tb_print_custom(col,       y+1, th->accent, th->bg, SYM_ARROW);
    tb_print_custom(col+2,     y+1, th->fg,     th->bg, "Ping  :");
    draw_badge     (label_end, y+1, ping_status, th);
    draw_glow_bar  (label_end+8, y+1, ping_status, th, 10);
    tb_print_fixed (label_end+19, y+1, get_status_fg(ping_status,th), th->bg, ping, w-label_end-21+x);

    /* row 2: host */
    tb_print_custom(col,   y+2, th->accent, th->bg, SYM_HOST);
    snprintf(buf, sizeof(buf), "Host  : %s", host);
    tb_print_fixed(col+2, y+2, th->fg, th->bg, buf, w-4);

    /* row 3: check */
    char chk_status[16];
    derive_check_status(check, chk_status, sizeof(chk_status));
    tb_print_custom(col,        y+3, th->accent, th->bg, SYM_PULSE);
    tb_print_custom(col+2,      y+3, th->fg,     th->bg, "Check :");
    draw_badge     (label_end,  y+3, chk_status, th);
    draw_glow_bar  (label_end+8,y+3, chk_status, th, 10);
    tb_print_fixed (label_end+19, y+3, get_status_fg(chk_status,th), th->bg, check, w-label_end-21+x);

    /* row 4: timestamp */
    tb_print_custom(col,   y+4, th->accent, th->bg, SYM_CLOCK);
    snprintf(buf, sizeof(buf), "Chk   : %s", chk_ts);
    tb_print_fixed(col+2, y+4, th->skip, th->bg, buf, w-4);

    (void)h;
}

/* ─────────────────────────────────────────────────────────────────────────
 *  MAIN DRAW
 * ───────────────────────────────────────────────────────────────────────── */
void draw_ui(int anim_tick) {
    Theme *th = &themes[current_theme];
    tb_set_clear_attrs(th->fg, th->bg);
    tb_clear();

    int sw = tb_width();
    int bw = config.dash_w + 2;
    int bx = (sw - bw) / 2; if (bx < 0) bx = 0;
    char buf[1024];

    /* ═══════════════════════════════════════════════════════════════════
     *  HEADER  ─  full width double-line box
     * ═══════════════════════════════════════════════════════════════════ */
    int y = 0;
    draw_header_box(bx, y, bw, 4, th);

    /* Title: " ✶ SYNCMON ✶  Nextcloud HA Cluster Monitor " */
    snprintf(buf, sizeof(buf), " %s SYNCMON %s  Nextcloud HA Cluster Monitor", SYM_AI, SYM_AI);
    tb_print_center(bx+1, y+1, th->hdr_fg|TB_BOLD, th->hdr_bg, buf, bw-2);

    /* Sub-row: refresh + timestamp + spinner */
    const char* spin = get_spinner(anim_tick);
    snprintf(buf, sizeof(buf), "%s  Refresh %ds  |  Updated: %s  |  %s",
             spin, config.display_refresh, state.timestamp, spin);
    tb_print_center(bx+1, y+2, th->accent, th->hdr_bg, buf, bw-2);

    /* ═══════════════════════════════════════════════════════════════════
     *  OVERVIEW ROW  ─  Overall status + summary bars
     * ═══════════════════════════════════════════════════════════════════ */
    y = 4;
    draw_box(bx, y, bw, 5, th->box1, "Overview", th);

    /* Overall status badge on left */
    tb_print_custom(bx+2, y+1, th->fg, th->bg, "Overall Status :");
    draw_badge(bx+19, y+1, state.overall_status, th);

    /* MariaDB summary bar line */
    tb_print_custom(bx+2, y+2, th->highlight, th->bg, SYM_DB);
    tb_print_custom(bx+4, y+2, th->highlight, th->bg, " MariaDB :");
    snprintf(buf, sizeof(buf), "%s " SYM_ARROW " %s", state.m_m_ep, state.m_s_ep);
    tb_print_custom(bx+15, y+2, th->fg, th->bg, buf);
    /* status bars at right edge */
    draw_glow_bar(bx+bw-34, y+2, state.m_m_status, th, 10);
    tb_print_custom(bx+bw-24, y+2, th->skip, th->bg, SYM_LINK);
    draw_glow_bar(bx+bw-23, y+2, state.m_s_status, th, 10);
    tb_print_custom(bx+bw-13, y+2, th->skip, th->bg, SYM_LINK);
    draw_glow_bar(bx+bw-12, y+2, state.m_sync,     th, 10);

    /* Redis summary bar line */
    tb_print_custom(bx+2, y+3, th->highlight, th->bg, SYM_REDIS);
    tb_print_custom(bx+4, y+3, th->highlight, th->bg, " Redis   :");
    snprintf(buf, sizeof(buf), "%s " SYM_ARROW " %s", state.r_m_ep, state.r_s_ep);
    tb_print_custom(bx+15, y+3, th->fg, th->bg, buf);
    draw_glow_bar(bx+bw-34, y+3, state.r_m_status, th, 10);
    tb_print_custom(bx+bw-24, y+3, th->skip, th->bg, SYM_LINK);
    draw_glow_bar(bx+bw-23, y+3, state.r_s_status, th, 10);
    tb_print_custom(bx+bw-13, y+3, th->skip, th->bg, SYM_LINK);
    draw_glow_bar(bx+bw-12, y+3, state.r_sync,     th, 10);

    /* ═══════════════════════════════════════════════════════════════════
     *  AI ANALYSIS PANEL (conditional)
     * ═══════════════════════════════════════════════════════════════════ */
    y += 6;
    if (config.show_ai) {
        draw_box(bx, y, bw, 5, th->box1, SYM_AI " AI Analysis", th);
        /* animated pulse on left edge */
        tb_print_custom(bx+2, y+1, th->accent|TB_BOLD, th->bg, SPIN_PULSE[anim_tick%10]);
        tb_print_custom(bx+4, y+1, th->hdr_fg, th->bg, ai_line1);
        tb_print_custom(bx+2, y+2, th->ok,     th->bg, SYM_DB);
        tb_print_fixed (bx+4, y+2, th->fg,     th->bg, ai_line2, bw-6);
        tb_print_custom(bx+2, y+3, th->accent, th->bg, SYM_NC);
        tb_print_fixed (bx+4, y+3, th->fg,     th->bg, ai_line3, bw-6);
        /* separator line */
        tb_print_custom(bx+2, y+4, th->skip, th->bg, "─ Press 'a' to toggle ─");
        y += 6;
    }

    /* ═══════════════════════════════════════════════════════════════════
     *  ROW 1: Loadbalancer (66%) + DNS (34%), h=7
     * ═══════════════════════════════════════════════════════════════════ */
    int lb_w  = (bw * 2) / 3;
    int dns_w = bw - lb_w;
    draw_simple_panel(bx,      y, lb_w,  7, SYM_LB " Loadbalancer",
                      state.lb_host,  state.lb_ping_status, state.lb_ping,
                      state.lb_check, state.lb_chk, th);
    draw_simple_panel(bx+lb_w, y, dns_w, 7, SYM_DNS " DNS",
                      state.dns_host, state.dns_ping_status, state.dns_ping,
                      state.dns_check, state.dns_chk, th);

    /* ═══════════════════════════════════════════════════════════════════
     *  ROW 2: Nextcloud 1 (50%) + Nextcloud 2 (50%), h=7
     * ═══════════════════════════════════════════════════════════════════ */
    y += 8;
    int nc_w  = bw / 2;
    int nc2_w = bw - nc_w;
    draw_simple_panel(bx,      y, nc_w,  7, SYM_NC " Nextcloud 1",
                      state.nc1_host, state.nc1_ping_status, state.nc1_ping,
                      state.nc1_check, state.nc1_chk, th);
    draw_simple_panel(bx+nc_w, y, nc2_w, 7, SYM_NC " Nextcloud 2",
                      state.nc2_host, state.nc2_ping_status, state.nc2_ping,
                      state.nc2_check, state.nc2_chk, th);

    /* ═══════════════════════════════════════════════════════════════════
     *  ROW 3: NFS (33%) + MariaDB (33%) + Redis (33%), h=9
     * ═══════════════════════════════════════════════════════════════════ */
    y += 8;
    int third  = bw / 3;
    int third2 = bw / 3;
    int third3 = bw - third - third2;

    /* NFS */
    draw_simple_panel(bx, y, third, 7, SYM_NFS " NFS",
                      state.nfs_host, state.nfs_ping_status, state.nfs_ping,
                      state.nfs_check, state.nfs_chk, th);

    /* ── MariaDB detail box ─────────────────────────────────────────── */
    int mx = bx + third;
    draw_box(mx, y, third2, 9, th->box2, SYM_DB " MariaDB", th);

    tb_print_custom(mx+2, y+1, th->accent, th->bg, SYM_MASTER);
    tb_print_custom(mx+4, y+1, th->fg,     th->bg, "Master :");
    draw_badge     (mx+13, y+1, state.m_m_status, th);
    draw_glow_bar  (mx+21, y+1, state.m_m_status, th, 10);

    snprintf(buf, sizeof(buf), "  %s %s", SYM_HOST, state.m_m_ep);
    tb_print_fixed(mx+2, y+2, th->fg, th->bg, buf, third2-4);

    tb_print_custom(mx+2, y+3, th->accent, th->bg, SYM_SLAVE);
    tb_print_custom(mx+4, y+3, th->fg,     th->bg, "Slave  :");
    draw_badge     (mx+13, y+3, state.m_s_status, th);
    draw_glow_bar  (mx+21, y+3, state.m_s_status, th, 10);

    snprintf(buf, sizeof(buf), "  %s %s", SYM_HOST, state.m_s_ep);
    tb_print_fixed(mx+2, y+4, th->fg, th->bg, buf, third2-4);

    tb_print_custom(mx+2, y+5, th->accent, th->bg, SYM_LINK);
    tb_print_custom(mx+4, y+5, th->fg,     th->bg, "Sync   :");
    draw_badge     (mx+13, y+5, state.m_sync, th);
    draw_glow_bar  (mx+21, y+5, state.m_sync, th, 10);

    int gtid_max = third2 - 4;
    format_shortened_left(buf, sizeof(buf), "  M-GTID: ", state.m_m_gtid, gtid_max);
    tb_print_fixed(mx+2, y+6, th->skip, th->bg, buf, third2-4);
    format_shortened_left(buf, sizeof(buf), "  S-GTID: ", state.m_s_gtid, gtid_max);
    tb_print_fixed(mx+2, y+7, th->skip, th->bg, buf, third2-4);

    /* ── Redis detail box ──────────────────────────────────────────────── */
    int rx = bx + third + third2;
    draw_box(rx, y, third3, 9, th->box2, SYM_REDIS " Redis", th);

    tb_print_custom(rx+2, y+1, th->accent, th->bg, SYM_MASTER);
    tb_print_custom(rx+4, y+1, th->fg,     th->bg, "Master :");
    draw_badge     (rx+13, y+1, state.r_m_status, th);
    draw_glow_bar  (rx+21, y+1, state.r_m_status, th, 10);

    snprintf(buf, sizeof(buf), "  %s %s", SYM_HOST, state.r_m_ep);
    tb_print_fixed(rx+2, y+2, th->fg, th->bg, buf, third3-4);

    tb_print_custom(rx+2, y+3, th->accent, th->bg, SYM_SLAVE);
    tb_print_custom(rx+4, y+3, th->fg,     th->bg, "Slave  :");
    draw_badge     (rx+13, y+3, state.r_s_status, th);
    draw_glow_bar  (rx+21, y+3, state.r_s_status, th, 10);

    snprintf(buf, sizeof(buf), "  %s %s", SYM_HOST, state.r_s_ep);
    tb_print_fixed(rx+2, y+4, th->fg, th->bg, buf, third3-4);

    tb_print_custom(rx+2, y+5, th->accent, th->bg, SYM_LINK);
    tb_print_custom(rx+4, y+5, th->fg,     th->bg, "Repl   :");
    draw_badge     (rx+13, y+5, state.r_sync, th);
    draw_glow_bar  (rx+21, y+5, state.r_sync, th, 10);

    snprintf(buf, sizeof(buf), "  Detail: %s", state.r_det);
    tb_print_fixed(rx+2, y+6, th->fg,  th->bg, buf, third3-4);
    snprintf(buf, sizeof(buf), "  %s %s", SYM_CLOCK, state.r_chk);
    tb_print_fixed(rx+2, y+7, th->skip,th->bg, buf, third3-4);

    /* ═══════════════════════════════════════════════════════════════════
     *  HISTORY BOX
     * ═══════════════════════════════════════════════════════════════════ */
    y += 10;
    draw_box(bx, y, bw, 5, th->box1, SYM_HISTORY " History", th);

    tb_print_custom(bx+2, y+1, th->highlight, th->bg, SYM_DB);
    tb_print_custom(bx+4, y+1, th->fg,        th->bg, "MariaDB sync :");
    draw_graph(bx+18, y+1, hist_mysql, hist_idx, bw-20, th);

    tb_print_custom(bx+2, y+2, th->highlight, th->bg, SYM_REDIS);
    tb_print_custom(bx+4, y+2, th->fg,        th->bg, "Redis  repl  :");
    draw_graph(bx+18, y+2, hist_redis, hist_idx, bw-20, th);

    snprintf(buf, sizeof(buf), "%s  %s", SYM_BULLET, state.message);
    tb_print_fixed(bx+2, y+3, th->accent, th->bg, buf, bw-4);

    /* ═══════════════════════════════════════════════════════════════════
     *  FOOTER  ─  key bindings legend
     * ═══════════════════════════════════════════════════════════════════ */
    y += 6;
    /* draw a thin separator */
    tb_hline(bx, y, 0x2508, th->skip, th->bg, bw);

    const char* keys[] = {
        " q  Quit",
        " t  Themes",
        " g  Graph",
        " s  Spinner",
        " a  AI Panel",
        NULL
    };
    int kx = bx + 1;
    for (int i = 0; keys[i]; i++) {
        tb_set_cell(kx, y+1, '[', th->skip, th->bg);
        tb_print_custom(kx+1, y+1, th->accent|TB_BOLD, th->bg, keys[i]);
        tb_set_cell(kx+1+(int)strlen(keys[i]), y+1, ']', th->skip, th->bg);
        kx += (int)strlen(keys[i]) + 4;
    }

    draw_theme_menu(th);
    tb_present();
}

/* ─────────────────────────────────────────────────────────────────────────
 *  USAGE + MAIN
 * ───────────────────────────────────────────────────────────────────────── */
static void print_usage(const char* prog) {
    printf("Usage: %s [OPTIONS]\n\n"
           "Options:\n"
           "  -r, --refresh <seconds>   Refresh interval in seconds (default: 2)\n"
           "  -f, --file    <path>      Path to state file (default: %s)\n"
           "  --test                    Run with simulated mock data\n"
           "  --no-braille              Use classic block graphs\n"
           "  -h, --help                Show this message\n\n"
           "Keyboard:\n"
           "  q / Ctrl+C  Quit\n"
           "  t           Theme menu (arrows + Enter)\n"
           "  g           Toggle graph style\n"
           "  s           Cycle spinner style\n"
           "  a           Toggle AI analysis panel\n",
           prog, STATE_FILE_DEFAULT);
}

int main(int argc, char** argv) {
    for (int i=0;i<HIST_SIZE;i++) { hist_mysql[i]=-1; hist_redis[i]=-1; }
    for (int i=1;i<argc;i++) {
        if      (strcmp(argv[i],"--test")==0)       config.test_mode=1;
        else if (strcmp(argv[i],"--no-braille")==0) config.use_braille=0;
        else if ((strcmp(argv[i],"-r")==0||strcmp(argv[i],"--refresh")==0) && i+1<argc) {
            config.display_refresh=atoi(argv[++i]);
            if(config.display_refresh<=0){fprintf(stderr,"ERROR: refresh must be >0\n");return 1;}
        } else if ((strcmp(argv[i],"-f")==0||strcmp(argv[i],"--file")==0) && i+1<argc) {
            strncpy(config.state_file,argv[++i],sizeof(config.state_file)-1);
        } else if (strcmp(argv[i],"--help")==0||strcmp(argv[i],"-h")==0) {
            print_usage(argv[0]); return 0;
        } else { fprintf(stderr,"ERROR: unknown argument: %s\n",argv[i]); return 1; }
    }
    if (config.display_refresh<=0) config.display_refresh=2;
    srand(time(NULL));
    tb_init();
    tb_set_output_mode(TB_OUTPUT_256);
    tb_set_input_mode(TB_INPUT_ESC);
    signal(SIGINT,handle_sig); signal(SIGTERM,handle_sig);

    struct tb_event ev;
    uint64_t last_update=0; int anim_tick=0;
    load_state(); last_update=get_time_ms();

    while (keep_running) {
        uint64_t now=get_time_ms();
        if(now-last_update>=(uint64_t)config.display_refresh*1000){load_state();last_update=now;}
        anim_tick=(now/100)%10;
        draw_ui(anim_tick);

        int res=tb_peek_event(&ev,100);
        if(res==TB_OK) {
            if(theme_menu_open) {
                if(ev.type==TB_EVENT_KEY) {
                    if     (ev.key==TB_KEY_ARROW_UP)    theme_menu_sel=(theme_menu_sel-1+num_themes)%num_themes;
                    else if(ev.key==TB_KEY_ARROW_DOWN)  theme_menu_sel=(theme_menu_sel+1)%num_themes;
                    else if(ev.key==TB_KEY_ENTER)       {current_theme=theme_menu_sel;theme_menu_open=0;}
                    else if(ev.key==TB_KEY_ESC||ev.ch=='q') theme_menu_open=0;
                }
            } else {
                if(ev.type==TB_EVENT_KEY) {
                    if     (ev.ch=='q'||ev.key==TB_KEY_CTRL_C) break;
                    else if(ev.ch=='g') config.use_braille=!config.use_braille;
                    else if(ev.ch=='t') {theme_menu_sel=current_theme;theme_menu_open=1;}
                    else if(ev.ch=='s') config.spinner_style=(config.spinner_style+1)%4;
                    else if(ev.ch=='a') config.show_ai=!config.show_ai;
                }
            }
        }
    }
    tb_shutdown();
    return 0;
}

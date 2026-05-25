/*
 * syncmon.c  ─  Nextcloud HA Cluster Monitor  [new-gui branch]
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
#include <ctype.h>
#include <stdint.h>

#define MAX_LINE  1024
#define MAX_VAL   256
#define HIST_SIZE 600

#define STATE_FILE_DEFAULT "/var/log/syncmon/syncmon_state.env"

/* symbols */
#define SYM_OK      "\u2714"
#define SYM_WARN    "\u26A0"
#define SYM_ERR     "\u2718"
#define SYM_SKIP    "\u25CB"
#define SYM_AI      "\u2736"
#define SYM_DB      "\u25BC"
#define SYM_REDIS   "\u25B6"
#define SYM_LB      "\u2248"
#define SYM_DNS     "\u2318"
#define SYM_NC      "\u2601"
#define SYM_NFS     "\u25A0"
#define SYM_CLOCK   "\u29D6"
#define SYM_PULSE   "\u25C9"

static const char* SPIN_BRAILLE[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
static const char* SPIN_DOTS[]    = {"⣾","⣽","⣻","⢿","⡿","⣟","⣯","⣷","⣾","⣽"};
static const char* SPIN_PULSE[]   = {"◉","◎","○","◎","◉","●","◉","◎","○","◎"};
static const char* SPIN_ARROW[]   = {"←","↖","↑","↗","→","↘","↓","↙","←","↖"};

typedef struct {
    const char* name;
    uint16_t bg, fg, box1, box2, highlight, accent, ok, warn, err, skip;
    uint16_t hdr_bg, hdr_fg, badge_bg;
} Theme;

Theme themes[] = {
    { "Default",     TB_DEFAULT,TB_DEFAULT, 13,12, 11|TB_BOLD,14|TB_BOLD, 46|TB_BOLD,226|TB_BOLD,196|TB_BOLD,244, 237,14,238 },
    { "Monokai",     235,252, 197,81, 148,208, 148,208,197,81,   234,208,237 },
    { "Dracula",     236,253, 212,117, 205,228, 84,228,203,117,  234,212,238 },
    { "Nord",        237,231, 109,110, 114,110, 114,220,174,109, 235,110,239 },
    { "Gruvbox",     235,223, 208,109, 214,175, 142,214,167,109, 234,214,237 },
    { "Cyberpunk",   233,253, 199,51, 226,39, 46,226,196,240,    232,201,235 },
    { "Calm",        235,252, 67,66, 109,151, 108,179,167,242,   234,67,237  },
    { "White Paper", 230,236, 240,242, 22,88, 28,130,124,246,    231,22,252  },
    { "Grayscale",   233,250, 240,244, 255,252, 246,250,255|TB_BOLD,238, 234,255,237 },
    { "Ocean",       234,195, 39,45, 51,81, 46,220,196,240,      232,45,236  },
    { "Lava",        233,223, 202,214, 220,196, 46,214,196,244,  232,202,235 }
};

int num_themes = sizeof(themes)/sizeof(Theme);
int current_theme = 0;
int theme_menu_open = 0;
int theme_menu_sel = 0;

struct {
    int display_refresh;
    char state_file[MAX_VAL];
    int dash_w;
    int use_braille;
    int spinner_style;
} config = {
    .display_refresh = 2,
    .state_file      = STATE_FILE_DEFAULT,
    .dash_w          = 92,
    .use_braille     = 1,
    .spinner_style   = 0
};

struct {
    char overall_status[MAX_VAL];
    char timestamp[MAX_VAL];
    char message[MAX_VAL];

    char m_m_status[MAX_VAL];
    char m_s_status[MAX_VAL];
    char m_sync[MAX_VAL];
    char m_m_gtid[MAX_VAL];
    char m_s_gtid[MAX_VAL];
    char m_chk[MAX_VAL];
    char m_m_ep[MAX_VAL];
    char m_s_ep[MAX_VAL];

    char r_m_status[MAX_VAL];
    char r_s_status[MAX_VAL];
    char r_sync[MAX_VAL];
    char r_det[MAX_VAL];
    char r_sent[MAX_VAL];
    char r_recv[MAX_VAL];
    char r_chk[MAX_VAL];
    char r_m_ep[MAX_VAL];
    char r_s_ep[MAX_VAL];

    char lb_host[MAX_VAL];
    char lb_ping[MAX_VAL];
    char lb_ping_status[MAX_VAL];
    char lb_check[MAX_VAL];
    char lb_chk[MAX_VAL];

    char dns_host[MAX_VAL];
    char dns_ping[MAX_VAL];
    char dns_ping_status[MAX_VAL];
    char dns_check[MAX_VAL];
    char dns_chk[MAX_VAL];

    char nc1_host[MAX_VAL];
    char nc1_ping[MAX_VAL];
    char nc1_ping_status[MAX_VAL];
    char nc1_check[MAX_VAL];
    char nc1_chk[MAX_VAL];

    char nc2_host[MAX_VAL];
    char nc2_ping[MAX_VAL];
    char nc2_ping_status[MAX_VAL];
    char nc2_check[MAX_VAL];
    char nc2_chk[MAX_VAL];

    char nfs_host[MAX_VAL];
    char nfs_ping[MAX_VAL];
    char nfs_ping_status[MAX_VAL];
    char nfs_check[MAX_VAL];
    char nfs_chk[MAX_VAL];
} state;

int keep_running = 1;

int hist_mysql[HIST_SIZE];
int hist_redis[HIST_SIZE];
int hist_lb_status[HIST_SIZE];
int hist_nc1_status[HIST_SIZE];
int hist_nc2_status[HIST_SIZE];
int hist_nfs_status[HIST_SIZE];
int hist_dns_status[HIST_SIZE];
int hist_idx = 0;

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
    snprintf(out, sz, "%s", val ? val : def);
}

void parse_env_file(const char* filepath) {
    FILE* f = fopen(filepath, "r");
    if (!f) return;

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line;
        char* val = eq + 1;
        val[strcspn(val, "\r\n")] = 0;

        if ((val[0] == '"' || val[0] == '\'') && strlen(val) > 1 && val[strlen(val)-1] == val[0]) {
            val[strlen(val)-1] = '\0';
            val++;
        }
        setenv(key, val, 1);
    }
    fclose(f);
}

int status_to_val(const char* s) {
    if (strcmp(s, "OK") == 0) return 4;
    if (strcmp(s, "WARN") == 0) return 2;
    if (strcmp(s, "ERROR") == 0) return 0;
    return -1;
}

static void derive_check_status(const char* check, char* out, size_t sz) {
    if (!check || strlen(check) == 0 || strcmp(check, "N/A") == 0) { snprintf(out, sz, "N/A"); return; }
    if (strncmp(check, "ERROR", 5) == 0) { snprintf(out, sz, "ERROR"); return; }
    if (strstr(check, "unreachable")) { snprintf(out, sz, "ERROR"); return; }
    if (strstr(check, "timeout")) { snprintf(out, sz, "ERROR"); return; }
    if (strstr(check, "NXDOMAIN")) { snprintf(out, sz, "ERROR"); return; }
    if (strstr(check, " 503") || strstr(check, " 502") || strstr(check, " 500") || strstr(check, " 504")) {
        snprintf(out, sz, "ERROR"); return;
    }
    if (strstr(check, "link=down")) { snprintf(out, sz, "ERROR"); return; }
    if (strstr(check, "no exports")) { snprintf(out, sz, "ERROR"); return; }
    if (strstr(check, "WARN") || strstr(check, "WARNING")) { snprintf(out, sz, "WARN"); return; }
    if (strstr(check, "maintenance=true")) { snprintf(out, sz, "WARN"); return; }
    if (strstr(check, " 4") && strstr(check, "HTTP")) { snprintf(out, sz, "WARN"); return; }

    {
        const char *bp = strstr(check, "backend ");
        if (bp) {
            int avail = 0, total = 0;
            if (sscanf(bp + 8, "%d/%d", &avail, &total) == 2 && total > 0 && avail < total) {
                snprintf(out, sz, "WARN");
                return;
            }
        }
    }
    snprintf(out, sz, "OK");
}

static void compute_ai_analysis() {
    int errors = 0, warns = 0;
    const char* checks[] = {
        state.overall_status, state.m_m_status, state.m_s_status, state.m_sync,
        state.r_m_status, state.r_s_status, state.r_sync,
        state.lb_ping_status, state.dns_ping_status, state.nc1_ping_status,
        state.nc2_ping_status, state.nfs_ping_status, NULL
    };

    for (int i = 0; checks[i]; i++) {
        if (strcmp(checks[i], "ERROR") == 0) errors++;
        else if (strcmp(checks[i], "WARN") == 0) warns++;
    }

    int total = 11;
    int healthy = total - errors - warns;
    if (healthy < 0) healthy = 0;
    int pct = (healthy * 100) / total;

    if (errors == 0 && warns == 0)
        snprintf(ai_line1, sizeof(ai_line1), "All systems nominal - cluster health 100%%");
    else if (errors == 0)
        snprintf(ai_line1, sizeof(ai_line1), "Health %d%% - %d warning(s) detected, no critical faults", pct, warns);
    else
        snprintf(ai_line1, sizeof(ai_line1), "Health %d%% - %d error(s), %d warning(s) require attention", pct, errors, warns);

    if (strcmp(state.m_sync, "OK") == 0 && strcmp(state.r_sync, "OK") == 0)
        snprintf(ai_line2, sizeof(ai_line2), "Replication stable: MariaDB GTID aligned, Redis replication healthy");
    else if (strcmp(state.m_sync, "OK") != 0 && strcmp(state.r_sync, "OK") != 0)
        snprintf(ai_line2, sizeof(ai_line2), "Replication issues on both MariaDB and Redis");
    else if (strcmp(state.m_sync, "OK") != 0)
        snprintf(ai_line2, sizeof(ai_line2), "MariaDB GTID mismatch detected - inspect master/slave drift");
    else
        snprintf(ai_line2, sizeof(ai_line2), "Redis replication degraded - inspect payload handoff");

    char parts[MAX_VAL] = "";
    if (strcmp(state.lb_ping_status, "OK") != 0) strncat(parts, "LoadBalancer ", sizeof(parts)-strlen(parts)-1);
    if (strcmp(state.nc1_ping_status, "OK") != 0 || strcmp(state.nc2_ping_status, "OK") != 0)
        strncat(parts, "Nextcloud ", sizeof(parts)-strlen(parts)-1);
    if (strcmp(state.nfs_ping_status, "OK") != 0) strncat(parts, "NFS ", sizeof(parts)-strlen(parts)-1);

    if (parts[0] == '\0')
        snprintf(ai_line3, sizeof(ai_line3), "Load balancer, Nextcloud nodes, and NFS are reachable");
    else
        snprintf(ai_line3, sizeof(ai_line3), "Unreachable nodes: %s- investigate connectivity", parts);
}

void format_shortened_left(char *buf, size_t bsz, const char* prefix, const char* val, int maxlen) {
    int pl = (int)strlen(prefix), vl = (int)strlen(val);
    if (pl + vl <= maxlen) snprintf(buf, bsz, "%s%s", prefix, val);
    else {
        int kl = maxlen - pl - 3;
        if (kl < 0) snprintf(buf, bsz, "%.*s", maxlen, prefix);
        else snprintf(buf, bsz, "%s...%s", prefix, val + (vl - kl));
    }
}

void load_state() {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char now[64];
    strftime(now, sizeof(now), "%Y-%m-%d %H:%M:%S", &tm);

    if (access(config.state_file, R_OK) == 0) parse_env_file(config.state_file);

    get_env_or("OVERALL_STATUS",            "NO DATA",             state.overall_status, sizeof(state.overall_status));
    get_env_or("SYNCMON_TIMESTAMP",         now,                   state.timestamp,      sizeof(state.timestamp));
    get_env_or("SYNCMON_MESSAGE",           "Waiting for daemon...", state.message,      sizeof(state.message));

    get_env_or("MYSQL_MASTER_STATUS",       "N/A",                 state.m_m_status, sizeof(state.m_m_status));
    get_env_or("MYSQL_SLAVE_STATUS",        "N/A",                 state.m_s_status, sizeof(state.m_s_status));
    get_env_or("MYSQL_SYNC_STATUS",         "N/A",                 state.m_sync,     sizeof(state.m_sync));
    get_env_or("MYSQL_MASTER_GTID",         "unknown",             state.m_m_gtid,   sizeof(state.m_m_gtid));
    get_env_or("MYSQL_SLAVE_GTID",          "unknown",             state.m_s_gtid,   sizeof(state.m_s_gtid));
    get_env_or("MYSQL_CHECK_TIMESTAMP",     state.timestamp,       state.m_chk,      sizeof(state.m_chk));

    get_env_or("REDIS_MASTER_STATUS",       "N/A",                 state.r_m_status, sizeof(state.r_m_status));
    get_env_or("REDIS_SLAVE_STATUS",        "N/A",                 state.r_s_status, sizeof(state.r_s_status));
    get_env_or("REDIS_REPLICATION_STATUS",  "N/A",                 state.r_sync,     sizeof(state.r_sync));
    get_env_or("REDIS_REPLICATION_DETAIL",  "not available",       state.r_det,      sizeof(state.r_det));
    get_env_or("REDIS_SENT_PAYLOAD",        "(none)",              state.r_sent,     sizeof(state.r_sent));
    get_env_or("REDIS_RECV_PAYLOAD",        "(none)",              state.r_recv,     sizeof(state.r_recv));
    get_env_or("REDIS_CHECK_TIMESTAMP",     state.timestamp,       state.r_chk,      sizeof(state.r_chk));

    char mh[64], mp[64], sh[64], sp[64];
    get_env_or("MYSQL_MASTER_HOST", "unknown", mh, sizeof(mh));
    get_env_or("MYSQL_MASTER_PORT", "?",       mp, sizeof(mp));
    snprintf(state.m_m_ep, sizeof(state.m_m_ep), "%s:%s", mh, mp);

    get_env_or("MYSQL_SLAVE_HOST",  "unknown", sh, sizeof(sh));
    get_env_or("MYSQL_SLAVE_PORT",  "?",       sp, sizeof(sp));
    snprintf(state.m_s_ep, sizeof(state.m_s_ep), "%s:%s", sh, sp);

    get_env_or("REDIS_MASTER_HOST", "unknown", mh, sizeof(mh));
    get_env_or("REDIS_MASTER_PORT", "?",       mp, sizeof(mp));
    snprintf(state.r_m_ep, sizeof(state.r_m_ep), "%s:%s", mh, mp);

    get_env_or("REDIS_SLAVE_HOST",  "unknown", sh, sizeof(sh));
    get_env_or("REDIS_SLAVE_PORT",  "?",       sp, sizeof(sp));
    snprintf(state.r_s_ep, sizeof(state.r_s_ep), "%s:%s", sh, sp);

#define LOAD_COMP(pfx, KEY) \
    get_env_or(KEY "_HOST",            "N/A",   state.pfx##_host,        sizeof(state.pfx##_host)); \
    get_env_or(KEY "_PING",            "N/A",   state.pfx##_ping,        sizeof(state.pfx##_ping)); \
    get_env_or(KEY "_PING_STATUS",     "N/A",   state.pfx##_ping_status, sizeof(state.pfx##_ping_status)); \
    get_env_or(KEY "_CHECK",           "N/A",   state.pfx##_check,       sizeof(state.pfx##_check)); \
    get_env_or(KEY "_CHECK_TIMESTAMP", "never", state.pfx##_chk,         sizeof(state.pfx##_chk));

    LOAD_COMP(lb,  "LB")
    LOAD_COMP(dns, "DNS")
    LOAD_COMP(nc1, "NC1")
    LOAD_COMP(nc2, "NC2")
    LOAD_COMP(nfs, "NFS")
#undef LOAD_COMP

    hist_mysql[hist_idx] = status_to_val(state.m_sync);
    hist_redis[hist_idx] = status_to_val(state.r_sync);

    char chk_lb[16], chk_nc1[16], chk_nc2[16], chk_nfs[16], chk_dns[16];
    derive_check_status(state.lb_check, chk_lb, sizeof(chk_lb));
    derive_check_status(state.nc1_check, chk_nc1, sizeof(chk_nc1));
    derive_check_status(state.nc2_check, chk_nc2, sizeof(chk_nc2));
    derive_check_status(state.nfs_check, chk_nfs, sizeof(chk_nfs));
    derive_check_status(state.dns_check, chk_dns, sizeof(chk_dns));

    hist_lb_status[hist_idx]  = status_to_val(chk_lb);
    hist_nc1_status[hist_idx] = status_to_val(chk_nc1);
    hist_nc2_status[hist_idx] = status_to_val(chk_nc2);
    hist_nfs_status[hist_idx] = status_to_val(chk_nfs);
    hist_dns_status[hist_idx] = status_to_val(chk_dns);
    
    hist_idx = (hist_idx + 1) % HIST_SIZE;

    compute_ai_analysis();
}

/* drawing primitives */
int tb_print_custom(int x, int y, uint16_t fg, uint16_t bg, const char *str) {
    while (*str) {
        uint32_t u;
        str += tb_utf8_char_to_unicode(&u, str);
        tb_set_cell(x++, y, u, fg, bg);
    }
    return 0;
}

void tb_print_fixed(int x, int y, uint16_t fg, uint16_t bg, const char *str, int w) {
    int p = 0;
    while (*str && p < w) {
        uint32_t u;
        str += tb_utf8_char_to_unicode(&u, str);
        tb_set_cell(x++, y, u, fg, bg);
        p++;
    }
    while (p++ < w) tb_set_cell(x++, y, ' ', fg, bg);
}

void tb_print_center(int x, int y, uint16_t fg, uint16_t bg, const char *str, int w) {
    int len = 0;
    const char *p = str;
    while (*p) {
        uint32_t u;
        p += tb_utf8_char_to_unicode(&u, p);
        len++;
    }
    int pad = (w - len) / 2;
    if (pad < 0) pad = 0;
    tb_print_fixed(x, y, fg, bg, "", w);
    tb_print_custom(x + pad, y, fg, bg, str);
}

void tb_fill(int x, int y, int w, int h, uint16_t fg, uint16_t bg) {
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
            tb_set_cell(x + c, y + r, ' ', fg, bg);
}

void tb_hline(int x, int y, uint32_t ch, uint16_t fg, uint16_t bg, int len) {
    for (int i = 0; i < len; i++) tb_set_cell(x + i, y, ch, fg, bg);
}

uint16_t get_status_fg(const char* s, Theme* th) {
    if (strcmp(s, "OK") == 0) return th->ok;
    if (strcmp(s, "WARN") == 0) return th->warn;
    if (strcmp(s, "ERROR") == 0) return th->err;
    return th->skip;
}

/*
 * draw_box_wave: animates a border of a box with a travelling
 * sine-like wave to symbolise the dot squeezing through the wall.
 * wave_pos is the 1-based interior column index of the wave peak.
 */
static void draw_box_wave(int x, int y_border, int box_w, uint16_t fg, uint16_t bg, int wave_pos) {
    for (int i = 1; i < box_w - 1; i++) {
        int d = i - wave_pos;
        uint32_t ch;
        uint16_t wfg = fg;
        if (d == 0) {
            ch  = 0x2501;          /* ━ heavy horizontal – compression peak  */
            wfg = fg | TB_BOLD;
        } else if (d == -1 || d == 1) {
            ch  = 0x2508;          /* ┈ light quad-dash   – dip              */
            wfg = fg | TB_BOLD;
        } else if (d == -2 || d == 2) {
            ch  = 0x2504;          /* ┄ light triple-dash – outer ripple     */
        } else {
            ch  = 0x2500;          /* ─ normal                               */
        }
        tb_set_cell(x + i, y_border, ch, wfg, bg);
    }
}

void draw_box(int x, int y, int w, int h, uint16_t fg, const char* title, Theme* th) {
    tb_set_cell(x,     y,     0x256D, fg, th->bg);
    tb_set_cell(x+w-1, y,     0x256E, fg, th->bg);
    tb_set_cell(x,     y+h-1, 0x2570, fg, th->bg);
    tb_set_cell(x+w-1, y+h-1, 0x256F, fg, th->bg);

    for (int i = 1; i < w-1; i++) {
        tb_set_cell(x+i, y,     0x2500, fg, th->bg);
        tb_set_cell(x+i, y+h-1, 0x2500, fg, th->bg);
    }
    for (int i = 1; i < h-1; i++) {
        tb_set_cell(x,     y+i, 0x2502, fg, th->bg);
        tb_set_cell(x+w-1, y+i, 0x2502, fg, th->bg);
    }

    if (title) {
        int tl = 0;
        const char* tp = title;
        while (*tp) { uint32_t u; tp += tb_utf8_char_to_unicode(&u, tp); tl++; }
        int tx = x + (w - tl - 4) / 2;
        if (tx < x + 1) tx = x + 1;

        tb_set_cell(tx, y, ' ', th->bg, th->bg);
        tb_set_cell(tx+1, y, 0x25C8, th->accent, th->bg);
        tb_set_cell(tx+2, y, ' ', th->bg, th->bg);
        tb_print_custom(tx+3, y, th->hdr_fg|TB_BOLD, th->bg, title);
        tb_set_cell(tx+3+tl,   y, ' ', th->bg, th->bg);
        tb_set_cell(tx+3+tl+1, y, 0x25C8, th->accent, th->bg);
        tb_set_cell(tx+3+tl+2, y, ' ', th->bg, th->bg);
    }
}

void draw_corner_title_box(int x, int y, int w, int h,
                           const char* left_title,
                           const char* center_title,
                           const char* right_title,
                           uint16_t fg,
                           Theme* th)
{
    draw_box(x, y, w, h, fg, NULL, th);

    int ll = (int)strlen(left_title);
    int cl = (int)strlen(center_title);
    int rl = (int)strlen(right_title);

    int cx = x + (w - cl - 4) / 2;
    if (cx < x + ll + 4) cx = x + ll + 4;

    int rx = x + w - rl - 2;
    if (rx < cx + cl + 4) rx = cx + cl + 4;

    tb_print_custom(x + 1, y, th->hdr_fg | TB_BOLD, th->bg, left_title);
    tb_print_custom(cx,    y, th->hdr_fg | TB_BOLD, th->bg, center_title);
    tb_print_custom(rx,    y, th->hdr_fg | TB_BOLD, th->bg, right_title);
}

void draw_header_box(int x, int y, int w, int h, Theme* th) {
    tb_set_cell(x,     y,     0x2554, th->box1, th->hdr_bg);
    tb_set_cell(x+w-1, y,     0x2557, th->box1, th->hdr_bg);
    tb_set_cell(x,     y+h-1, 0x255A, th->box1, th->hdr_bg);
    tb_set_cell(x+w-1, y+h-1, 0x255D, th->box1, th->hdr_bg);

    for (int i = 1; i < w-1; i++) {
        tb_set_cell(x+i, y,     0x2550, th->box1, th->hdr_bg);
        tb_set_cell(x+i, y+h-1, 0x2550, th->box1, th->hdr_bg);
    }
    for (int i = 1; i < h-1; i++) {
        tb_fill(x, y+i, w, 1, th->hdr_fg, th->hdr_bg);
        tb_set_cell(x,     y+i, 0x2551, th->box1, th->hdr_bg);
        tb_set_cell(x+w-1, y+i, 0x2551, th->box1, th->hdr_bg);
    }
}

void draw_badge(int x, int y, const char* s, Theme* th) {
    uint16_t fg = get_status_fg(s, th);
    uint16_t bg = th->badge_bg;
    const char *sym, *label;

    if      (strcmp(s, "OK") == 0)    { sym = SYM_OK;   label = "OK   "; }
    else if (strcmp(s, "WARN") == 0)  { sym = SYM_WARN; label = "WARN "; }
    else if (strcmp(s, "ERROR") == 0) { sym = SYM_ERR;  label = "ERROR"; }
    else                              { sym = SYM_SKIP; label = "N/A  "; }

    for (int i = 0; i < 9; i++) tb_set_cell(x+i, y, ' ', fg, bg);
    tb_print_custom(x+1, y, fg|TB_BOLD, bg, sym);
    tb_print_fixed(x+3, y, fg|TB_BOLD, bg, label, 5);
}

void draw_glow_bar(int x, int y, const char* s, Theme* th, int bar_w) {
    int fill = 0;
    if      (strcmp(s, "OK") == 0)    fill = bar_w;
    else if (strcmp(s, "WARN") == 0)  fill = (bar_w * 5) / 8;
    else if (strcmp(s, "ERROR") == 0) fill = (bar_w * 2) / 8;
    else                              fill = 1;

    uint16_t fg = get_status_fg(s, th);
    for (int i = 0; i < bar_w; i++) {
        tb_set_cell(x+i, y, (i < fill) ? 0x2588 : 0x2591, (i < fill) ? fg : th->skip, th->bg);
    }
}

uint32_t braille_bar(int h1, int h2) {
    if (h1 < 0) h1 = 0; if (h1 > 4) h1 = 4;
    if (h2 < 0) h2 = 0; if (h2 > 4) h2 = 4;
    uint32_t b = 0x2800;
    if (h1>=1) b|=0x40; if (h1>=2) b|=0x04; if (h1>=3) b|=0x02; if (h1>=4) b|=0x01;
    if (h2>=1) b|=0x80; if (h2>=2) b|=0x20; if (h2>=3) b|=0x10; if (h2>=4) b|=0x08;
    return b;
}

void draw_status_graph(int x, int y, int* hd, int head, int mw, Theme* th) {
    for (int c = 0; c < mw; c++) {
        int idx = head - 1 - (mw - 1 - c);
        while (idx < 0) idx += HIST_SIZE;
        idx %= HIST_SIZE;
        int v = hd[idx];

        uint16_t col;
        uint32_t braille_ch;
        uint32_t block_ch;

        if (v == 4) { // OK -> Good color, 8 dots high (4 rows)
            col = th->ok;
            braille_ch = braille_bar(4, 4);
            block_ch = 0x2588; // Full block
        } else if (v == 2) { // WARN -> Warn color, 6 dots high (3 rows)
            col = th->warn;
            braille_ch = braille_bar(3, 3);
            block_ch = 0x2586; // 3/4 block
        } else if (v == 0) { // ERROR -> Error color, 2 dots high (1 row)
            col = th->err;
            braille_ch = braille_bar(1, 1);
            block_ch = 0x2582; // 1/4 block
        } else { // SKIP/UNKNOWN -> Grayed out, 1 dot high (bottom left dot)
            col = th->skip;
            braille_ch = 0x2840; // dot 7
            block_ch = 0x2581; // 1/8 block
        }

        if (config.use_braille) {
            tb_set_cell(x+c, y, braille_ch, col, th->bg);
        } else {
            tb_set_cell(x+c, y, block_ch, col, th->bg);
        }
    }
}

const char* get_spinner(int tick) {
    switch (config.spinner_style) {
        case 1: return SPIN_DOTS[tick % 10];
        case 2: return SPIN_PULSE[tick % 10];
        case 3: return SPIN_ARROW[tick % 10];
        default:return SPIN_BRAILLE[tick % 10];
    }
}

void draw_theme_menu(Theme* th) {
    if (!theme_menu_open) return;

    int w = 36;
    int h = num_themes + 4;
    int sx = (tb_width() - w) / 2;
    int sy = (tb_height() - h) / 2;

    tb_fill(sx, sy, w, h, th->fg, th->bg);
    draw_box(sx, sy, w, h, th->accent, "Select Theme", th);
    tb_print_center(sx+1, sy+1, th->accent, th->bg, "↑/↓ navigate  Enter select  Esc cancel", w-2);
    tb_hline(sx+1, sy+2, 0x2508, th->skip, th->bg, w-2);

    for (int i = 0; i < num_themes; i++) {
        uint16_t ibg = (i == theme_menu_sel) ? th->accent : th->bg;
        uint16_t ifg = (i == theme_menu_sel) ? th->bg : th->fg;
        tb_fill(sx+2, sy+3+i, w-4, 1, ifg, ibg);
        tb_print_fixed(sx+3, sy+3+i, ifg|(i==theme_menu_sel?TB_BOLD:0), ibg, themes[i].name, w-6);
    }
}

void draw_node_panel(int x, int y, int w, int h,
                     const char* title,
                     const char* host,
                     const char* ping_status,
                     const char* ping,
                     const char* check,
                     const char* chk_ts,
                     Theme* th) {
    char full_title[256];
    if (host && host[0] != '\0' && strcmp(host, "N/A") != 0)
        snprintf(full_title, sizeof(full_title), "%s : %s", title, host);
    else
        snprintf(full_title, sizeof(full_title), "%s", title);

    draw_box(x, y, w, h, th->box2, full_title, th);

    int bar_w   = 10;
    int label_x = x + 2;
    int badge_x = x + 12;
    int bar_x   = x + 22;
    int text_x  = x + 34;
    int text_w  = w - (text_x - x) - 2;
    if (text_w < 1) text_w = 1;

    tb_print_custom(label_x, y+1, th->accent, th->bg, "Ping  :");
    draw_badge(badge_x, y+1, ping_status, th);
    draw_glow_bar(bar_x, y+1, ping_status, th, bar_w);
    tb_print_fixed(text_x, y+1, get_status_fg(ping_status, th), th->bg, ping, text_w);

    char chk_status[16];
    derive_check_status(check, chk_status, sizeof(chk_status));

    tb_print_custom(label_x, y+2, th->accent, th->bg, "Check :");
    draw_badge

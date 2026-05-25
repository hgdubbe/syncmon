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
int hist_lb_ping[HIST_SIZE];
int hist_nc1_ping[HIST_SIZE];
int hist_nc2_ping[HIST_SIZE];
int hist_nfs_ping[HIST_SIZE];
int hist_dns_ping[HIST_SIZE];
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

int parse_ping_ms(const char* s) {
    if (!s || !*s) return -1;
    while (*s && !isdigit((unsigned char)*s)) s++;
    if (!*s) return -1;
    return atoi(s);
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

    hist_mysql[hist_idx]    = status_to_val(state.m_sync);
    hist_redis[hist_idx]    = status_to_val(state.r_sync);
    hist_lb_ping[hist_idx]  = parse_ping_ms(state.lb_ping);
    hist_nc1_ping[hist_idx] = parse_ping_ms(state.nc1_ping);
    hist_nc2_ping[hist_idx] = parse_ping_ms(state.nc2_ping);
    hist_nfs_ping[hist_idx] = parse_ping_ms(state.nfs_ping);
    hist_dns_ping[hist_idx] = parse_ping_ms(state.dns_ping);
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
 * draw_box_wave: draws the bottom border of a box with a travelling sine-like
 * wave through the horizontal line characters, to symbolise the dot squeezing
 * through.  wave_pos is the position of the wave peak (0 = left edge, box_w-1
 * = right edge).  When wave_pos < 0 the function draws a normal flat border.
 */
static void draw_box_wave(int x, int y_bottom, int box_w, uint16_t fg, uint16_t bg,
                          int wave_pos)
{
    /* Wave shape: the "peak" cell uses a heavy block ━ (U+2501),
       the two neighbours use light wavy ≈ (U+2248 repurposed as
       a wave symbol), everything else stays ─ (U+2500).
       Corner cells are left untouched.                              */
    for (int i = 1; i < box_w - 1; i++) {
        int d = i - wave_pos;
        uint32_t ch;
        uint16_t wfg = fg;
        if (d == 0) {
            ch   = 0x2501;   /* ━ heavy horizontal */
            wfg  = fg | TB_BOLD;
        } else if (d == -1 || d == 1) {
            ch   = 0x2508;   /* ┈ light quadruple-dash (dip) */
            wfg  = fg | TB_BOLD;
        } else if (d == -2 || d == 2) {
            ch   = 0x2504;   /* ┄ light triple-dash (ripple) */
        } else {
            ch   = 0x2500;   /* ─ normal */
        }
        tb_set_cell(x + i, y_bottom, ch, wfg, bg);
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

/*
 * draw_box_with_wave: same as draw_box but animates the bottom border.
 * wave_pos < 0 → no wave (normal static border).
 */
static void draw_box_with_wave(int x, int y, int w, int h, uint16_t fg,
                               const char* title, Theme* th, int wave_pos)
{
    draw_box(x, y, w, h, fg, title, th);
    if (wave_pos >= 0)
        draw_box_wave(x, y + h - 1, w, fg, th->bg, wave_pos);
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

    tb_print_custom(x + 1, y, th->accent, th->bg, left_title);
    tb_print_custom(cx,    y, th->hdr_fg | TB_BOLD, th->bg, center_title);
    tb_print_custom(rx,    y, th->accent, th->bg, right_title);
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

        if (v == -1) { tb_set_cell(x+c, y, 0x2508, th->skip, th->bg); continue; }
        if (v == 0)  { tb_set_cell(x+c, y, 0x2588, th->err|TB_BOLD, th->bg); continue; }

        uint16_t col = (v == 2) ? th->warn : th->ok;
        if (config.use_braille) tb_set_cell(x+c, y, braille_bar(v, v), col, th->bg);
        else {
            uint32_t ch = (v == 2) ? 0x2585 : 0x2588;
            tb_set_cell(x+c, y, ch, col, th->bg);
        }
    }
}

void draw_ping_graph(int x, int y, int* hd, int head, int mw, Theme* th) {
    for (int c = 0; c < mw; c++) {
        int idx = head - 1 - (mw - 1 - c);
        while (idx < 0) idx += HIST_SIZE;
        idx %= HIST_SIZE;
        int v = hd[idx];

        if (v == -1) {
            tb_set_cell(x+c, y, 0x2508, th->skip, th->bg);
            continue;
        }
        if (v < 0) {
            tb_set_cell(x+c, y, 0x2588, th->err|TB_BOLD, th->bg);
            continue;
        }

        int h;
        uint16_t col;
        if (v == 0) {
            h = 1; col = th->warn;
        } else if (v <= 30) {
            h = 4; col = th->ok;
        } else if (v <= 100) {
            h = 2; col = th->warn;
        } else {
            h = 1; col = th->err;
        }

        if (config.use_braille) {
            tb_set_cell(x+c, y, braille_bar(h, h), col, th->bg);
        } else {
            static const uint32_t blocks[] = {0x2582, 0x2584, 0x2586, 0x2588};
            tb_set_cell(x+c, y, blocks[h-1], col, th->bg);
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
    draw_badge(badge_x, y+2, chk_status, th);
    draw_glow_bar(bar_x, y+2, chk_status, th, bar_w);
    tb_print_fixed(text_x, y+2, get_status_fg(chk_status, th), th->bg, check, text_w);

    char buf[MAX_VAL];
    snprintf(buf, sizeof(buf), "%s Checked: %s", SYM_CLOCK, chk_ts);
    tb_print_fixed(label_x, y+3, th->skip, th->bg, buf, w-4);
}

void draw_vertical_text(int x, int y, const char* s, uint16_t fg, uint16_t bg) {
    for (int i = 0; s[i]; i++) tb_set_cell(x, y + i, (uint32_t)(unsigned char)s[i], fg, bg);
}

void draw_center_sync_status(int x, int y, int w, const char* label, const char* status, Theme* th) {
    int ll = (int)strlen(label);
    int total = ll + 2 + 9;
    int sx = x + (w - total) / 2;
    if (sx < x + 1) sx = x + 1;

    tb_print_custom(sx, y, th->accent, th->bg, label);
    tb_print_custom(sx + ll + 1, y, th->fg, th->bg, ":");
    draw_badge(sx + ll + 3, y, status, th);
}

void draw_endpoint_status_left(int px, int py,
                               const char* role,
                               const char* ping_status,
                               const char* check_status,
                               Theme* th,
                               int* hook_x,
                               int* hook_y)
{
    int role_x  = px + 1;
    int text_x  = px + 3;
    int colon_x = px + 9;
    int badge_x = px + 11;

    draw_vertical_text(role_x, py, role, th->accent, th->bg);

    tb_print_custom(text_x, py,   th->accent, th->bg, "Ping");
    tb_print_custom(colon_x, py,  th->fg,     th->bg, ":");
    draw_badge(badge_x, py, ping_status, th);

    tb_print_custom(text_x, py+1, th->accent, th->bg, "Check");
    tb_print_custom(colon_x, py+1, th->fg,    th->bg, ":");
    draw_badge(badge_x, py+1, check_status, th);

    *hook_x = badge_x + 4;
    *hook_y = py + 2;
}

void draw_endpoint_status_right(int px, int pw, int py,
                                const char* role,
                                const char* ping_status,
                                const char* check_status,
                                Theme* th,
                                int* hook_x,
                                int* hook_y)
{
    int role_x  = px + pw - 2;
    int badge_x = px + pw - 21;
    int colon_x = badge_x + 10;
    int text_x  = badge_x + 12;

    draw_badge(badge_x, py, ping_status, th);
    tb_print_custom(colon_x, py, th->fg, th->bg, ":");
    tb_print_custom(text_x, py, th->accent, th->bg, "Ping");

    draw_badge(badge_x, py+1, check_status, th);
    tb_print_custom(colon_x, py+1, th->fg, th->bg, ":");
    tb_print_custom(text_x, py+1, th->accent, th->bg, "Check");

    draw_vertical_text(role_x, py, role, th->accent, th->bg);

    *hook_x = badge_x + 4;
    *hook_y = py + 2;
}

/*
 * draw_sync_path_between_hooks
 *
 * Changes vs original:
 *  1. Arrow fix: arrowhead placed at the last cell immediately before the
 *     status box (x2-1 for dir>0, x1+1 for dir<0) rather than x2-2 / x1+2.
 *  2. Wave animation: while the dot is inside the tunnel (tunnel_delay phase)
 *     the bottom border of the status box is animated with draw_box_wave.
 */
void draw_sync_path_between_hooks(int x1, int x2, int hook_y,
                                  int box_x, int box_y, int box_w, int box_h,
                                  int dir,
                                  const char* master_status,
                                  const char* sync_status,
                                  const char* slave_status,
                                  Theme* th, int tick)
{
    uint16_t col = get_status_fg(sync_status, th);
    if (x2 <= x1) return;

    int rail_y = box_y + box_h / 2;
    int left_gap  = box_x - x1;
    int right_gap = x2 - (box_x + box_w) + 1;
    if (left_gap  < 0) left_gap  = 0;
    if (right_gap < 0) right_gap = 0;

    /* vertical stems */
    for (int y = hook_y; y < rail_y; y++) {
        tb_set_cell(x1, y, 0x2502, th->skip, th->bg);
        tb_set_cell(x2, y, 0x2502, th->skip, th->bg);
    }

    /* corners */
    tb_set_cell(x1, rail_y, 0x2514, th->skip, th->bg);
    tb_set_cell(x2, rail_y, 0x2518, th->skip, th->bg);

    /* horizontal rails (skip over box) */
    for (int x = x1 + 1; x < x2; x++) {
        if (x < box_x || x >= box_x + box_w)
            tb_set_cell(x, rail_y, 0x2500, th->skip, th->bg);
    }

    /* ── FIX: arrowhead at the last cell ADJACENT to the box ── */
    if (strcmp(sync_status, "ERROR") != 0 && strcmp(slave_status, "ERROR") != 0) {
        if (dir > 0) {
            /* arrow points right → place ▶ at the cell just before the box */
            int arr_x = box_x - 1;
            if (arr_x > x1)
                tb_set_cell(arr_x, rail_y, 0x25B6, col | TB_BOLD, th->bg);
        } else {
            /* arrow points left → place ◀ at the cell just after the box */
            int arr_x = box_x + box_w;
            if (arr_x < x2)
                tb_set_cell(arr_x, rail_y, 0x25C0, col | TB_BOLD, th->bg);
        }
    }

    if (strcmp(master_status, "ERROR") == 0) return;

    int vert_len     = rail_y - hook_y;
    int tunnel_delay = (box_w > 36) ? (box_w - 36) : 0;
    int total_len    = vert_len + left_gap + tunnel_delay + right_gap + vert_len;
    if (total_len <= 0) return;

    int pos   = tick % total_len;
    int dot_x = -1, dot_y = -1;

    /* track whether the dot is inside the tunnel this frame */
    int in_tunnel = 0;
    int tunnel_phase_pos = 0; /* position within the tunnel [0, tunnel_delay) */

    if (dir > 0) {
        if (pos < vert_len) {
            dot_x = x1;
            dot_y = hook_y + pos;
        } else if (pos < vert_len + left_gap) {
            dot_x = x1 + (pos - vert_len);
            dot_y = rail_y;
        } else if (pos < vert_len + left_gap + tunnel_delay) {
            in_tunnel        = 1;
            tunnel_phase_pos = pos - vert_len - left_gap;
            dot_x = -1;
            dot_y = -1;
        } else if (pos < vert_len + left_gap + tunnel_delay + right_gap) {
            dot_x = box_x + box_w + (pos - vert_len - left_gap - tunnel_delay);
            dot_y = rail_y;
        } else {
            dot_x = x2;
            dot_y = (rail_y - 1) - (pos - vert_len - left_gap - tunnel_delay - right_gap);
        }

        if ((strcmp(sync_status, "ERROR") == 0 || strcmp(slave_status, "ERROR") == 0) &&
            dot_x > box_x + box_w / 2)
            dot_x = -1;
    } else {
        if (pos < vert_len) {
            dot_x = x2;
            dot_y = hook_y + pos;
        } else if (pos < vert_len + right_gap) {
            dot_x = x2 - (pos - vert_len);
            dot_y = rail_y;
        } else if (pos < vert_len + right_gap + tunnel_delay) {
            in_tunnel        = 1;
            tunnel_phase_pos = pos - vert_len - right_gap;
            dot_x = -1;
            dot_y = -1;
        } else if (pos < vert_len + right_gap + tunnel_delay + left_gap) {
            dot_x = (box_x - 1) - (pos - vert_len - right_gap - tunnel_delay);
            dot_y = rail_y;
        } else {
            dot_x = x1;
            dot_y = (rail_y - 1) - (pos - vert_len - right_gap - tunnel_delay - left_gap);
        }

        if ((strcmp(sync_status, "ERROR") == 0 || strcmp(slave_status, "ERROR") == 0) &&
            dot_x > -1 && dot_x < box_x + box_w / 2)
            dot_x = -1;
    }

    /* ── Wave animation when dot is inside the box ── */
    if (in_tunnel && tunnel_delay > 0) {
        /*
         * Map tunnel_phase_pos (0…tunnel_delay-1) onto the interior width of
         * the box bottom border (1…box_w-2), so the wave sweeps left-to-right
         * for dir>0 and right-to-left for dir<0.
         */
        int interior = box_w - 2;  /* cells between corners */
        int wave_interior_pos;
        if (dir > 0)
            wave_interior_pos = (tunnel_phase_pos * interior) / (tunnel_delay > 1 ? tunnel_delay - 1 : 1);
        else
            wave_interior_pos = interior - 1 - (tunnel_phase_pos * interior) / (tunnel_delay > 1 ? tunnel_delay - 1 : 1);

        /* clamp */
        if (wave_interior_pos < 0) wave_interior_pos = 0;
        if (wave_interior_pos >= interior) wave_interior_pos = interior - 1;

        /* wave_pos passed to draw_box_wave is relative to cell (box_x+1) */
        draw_box_wave(box_x, box_y + box_h - 1, box_w, col, th->bg, wave_interior_pos + 1);
    }

    if (dot_x != -1 && dot_y != -1)
        tb_set_cell(dot_x, dot_y, 0x25C6, col | TB_BOLD, th->bg);
}

void draw_mariadb_panel(int x, int y, int w, int h, int anim_tick, Theme* th) {
    draw_corner_title_box(x, y, w, h, state.m_m_ep, "MariaDB", state.m_s_ep, th->box2, th);

    int info_y = y + 1;
    int left_hook_x, left_hook_y;
    int right_hook_x, right_hook_y;

    draw_endpoint_status_left(x, info_y, "Master", "N/A", state.m_m_status, th, &left_hook_x, &left_hook_y);
    draw_endpoint_status_right(x, w, info_y, "Slave",  "N/A", state.m_s_status, th, &right_hook_x, &right_hook_y);

    draw_center_sync_status(x, y + 1, w, "Sync", state.m_sync, th);

    int box_w = 38;
    int box_h = 4;
    int box_x = x + (w - box_w) / 2;
    int box_y = y + 2;

    uint16_t sync_col = get_status_fg(state.m_sync, th);
    draw_box(box_x, box_y, box_w, box_h, sync_col, NULL, th);

    char buf[MAX_VAL];
    format_shortened_left(buf, sizeof(buf), "M-GTID: ", state.m_m_gtid, box_w - 4);
    tb_print_fixed(box_x + 2, box_y + 1, th->skip, th->bg, buf, box_w - 4);

    format_shortened_left(buf, sizeof(buf), "S-GTID: ", state.m_s_gtid, box_w - 4);
    tb_print_fixed(box_x + 2, box_y + 2, th->ok, th->bg, buf, box_w - 4);

    draw_sync_path_between_hooks(left_hook_x, right_hook_x, left_hook_y,
                                 box_x, box_y, box_w, box_h,
                                 1, state.m_m_status, state.m_sync, state.m_s_status, th, anim_tick);

    snprintf(buf, sizeof(buf), "%s Checked: %s", SYM_CLOCK, state.m_chk);
    tb_print_fixed(x + 2, y + h - 2, th->skip, th->bg, buf, w - 4);
}

void draw_redis_panel(int x, int y, int w, int h, int anim_tick, Theme* th) {
    draw_corner_title_box(x, y, w, h, state.r_s_ep, "Redis", state.r_m_ep, th->box2, th);

    int info_y = y + 1;
    int left_hook_x, left_hook_y;
    int right_hook_x, right_hook_y;

    draw_endpoint_status_left(x, info_y, "Slave",  "N/A", state.r_s_status, th, &left_hook_x, &left_hook_y);
    draw_endpoint_status_right(x, w, info_y, "Master", "N/A", state.r_m_status, th, &right_hook_x, &right_hook_y);

    draw_center_sync_status(x, y + 1, w, "Repl", state.r_sync, th);

    int box_w = 46;
    int box_h = 4;
    int box_x = x + (w - box_w) / 2;
    int box_y = y + 2;

    uint16_t sync_col = get_status_fg(state.r_sync, th);
    draw_box(box_x, box_y, box_w, box_h, sync_col, NULL, th);

    char buf[MAX_VAL];
    format_shortened_left(buf, sizeof(buf), "Sent : ", state.r_sent, box_w - 4);
    tb_print_fixed(box_x + 2, box_y + 1, th->skip, th->bg, buf, box_w - 4);

    int payload_match = (strcmp(state.r_sent, state.r_recv) == 0 &&
                         strcmp(state.r_sent, "(none)") != 0);
    uint16_t recv_col = payload_match ? th->ok : th->warn;
    if (strcmp(state.r_s_status, "ERROR") == 0 || strcmp(state.r_recv, "(no data)") == 0)
        recv_col = th->err;

    format_shortened_left(buf, sizeof(buf), "Recv : ", state.r_recv, box_w - 4);
    tb_print_fixed(box_x + 2, box_y + 2, recv_col, th->bg, buf, box_w - 4);

    draw_sync_path_between_hooks(left_hook_x, right_hook_x, left_hook_y,
                                 box_x, box_y, box_w, box_h,
                                 -1, state.r_m_status, state.r_sync, state.r_s_status, th, anim_tick);

    snprintf(buf, sizeof(buf), "%s Checked: %s", SYM_CLOCK, state.r_chk);
    tb_print_fixed(x + 2, y + h - 2, th->skip, th->bg, buf, w - 4);
}

void draw_ui(int anim_tick) {
    Theme* th = &themes[current_theme];

    int sw = tb_width();
    int bw = config.dash_w + 2;
    int bx = (sw - bw) / 2;
    if (bx < 0) bx = 0;

    char buf[1024];
    int y = 0;

    draw_header_box(bx, y, bw, 3, th);
    snprintf(buf, sizeof(buf), "%s SYNCMON %s  Nextcloud HA Cluster Monitor", SYM_AI, SYM_AI);
    tb_print_center(bx+1, y+1, th->hdr_fg|TB_BOLD, th->hdr_bg, buf, bw-2);

    const char* spin = get_spinner(anim_tick);
    snprintf(buf, sizeof(buf), "%s Refresh %ds  Updated: %s %s", spin, config.display_refresh, state.timestamp, spin);
    tb_print_center(bx+1, y+2, th->accent, th->hdr_bg, buf, bw-2);

    y = 4;
    int ov_h = 6;
    draw_box(bx, y, bw, ov_h, th->box1, "Overview", th);

    tb_print_custom(bx+2, y+1, th->fg, th->bg, "Overall Status:");
    draw_badge(bx+18, y+1, state.overall_status, th);

    tb_print_fixed(bx+2, y+2, th->hdr_fg, th->bg, ai_line1, bw-4);
    tb_print_fixed(bx+2, y+3, th->fg,     th->bg, ai_line2, bw-4);
    tb_print_fixed(bx+2, y+4, th->fg,     th->bg, ai_line3, bw-4);

    y += ov_h + 1;

    draw_node_panel(bx, y, bw, 5, SYM_LB " Loadbalancer",
                    state.lb_host, state.lb_ping_status, state.lb_ping,
                    state.lb_check, state.lb_chk, th);
    y += 6;

    int half = bw / 2;
    int half2 = bw - half;

    draw_node_panel(bx, y, half, 5, SYM_NC " Nextcloud 1",
                    state.nc1_host, state.nc1_ping_status, state.nc1_ping,
                    state.nc1_check, state.nc1_chk, th);
    draw_node_panel(bx + half, y, half2, 5, SYM_NC " Nextcloud 2",
                    state.nc2_host, state.nc2_ping_status, state.nc2_ping,
                    state.nc2_check, state.nc2_chk, th);
    y += 6;

    draw_mariadb_panel(bx, y, bw, 8, anim_tick, th);
    y += 9;

    draw_redis_panel(bx, y, bw, 8, anim_tick, th);
    y += 9;

    draw_node_panel(bx, y, half, 5, SYM_NFS " NFS",
                    state.nfs_host, state.nfs_ping_status, state.nfs_ping,
                    state.nfs_check, state.nfs_chk, th);
    draw_node_panel(bx + half, y, half2, 5, SYM_DNS " DNS",
                    state.dns_host, state.dns_ping_status, state.dns_ping,
                    state.dns_check, state.dns_chk, th);
    y += 6;

    draw_box(bx, y, bw, 10, th->box1, "History", th);

    tb_print_custom(bx+2, y+1, th->highlight, th->bg, SYM_DB);
    tb_print_custom(bx+4, y+1, th->fg, th->bg, "MariaDB sync:");
    draw_status_graph(bx+19, y+1, hist_mysql, hist_idx, bw-21, th);

    tb_print_custom(bx+2, y+2, th->highlight, th->bg, SYM_REDIS);
    tb_print_custom(bx+4, y+2, th->fg, th->bg, "Redis repl :");
    draw_status_graph(bx+19, y+2, hist_redis, hist_idx, bw-21, th);

    tb_print_custom(bx+2, y+3, th->highlight, th->bg, SYM_LB);
    tb_print_custom(bx+4, y+3, th->fg, th->bg, "LB ping    :");
    draw_ping_graph(bx+19, y+3, hist_lb_ping, hist_idx, bw-21, th);

    tb_print_custom(bx+2, y+4, th->highlight, th->bg, SYM_NC);
    tb_print_custom(bx+4, y+4, th->fg, th->bg, "NC1 ping   :");
    draw_ping_graph(bx+19, y+4, hist_nc1_ping, hist_idx, bw-21, th);

    tb_print_custom(bx+2, y+5, th->highlight, th->bg, SYM_NC);
    tb_print_custom(bx+4, y+5, th->fg, th->bg, "NC2 ping   :");
    draw_ping_graph(bx+19, y+5, hist_nc2_ping, hist_idx, bw-21, th);

    tb_print_custom(bx+2, y+6, th->highlight, th->bg, SYM_NFS);
    tb_print_custom(bx+4, y+6, th->fg, th->bg, "NFS ping   :");
    draw_ping_graph(bx+19, y+6, hist_nfs_ping, hist_idx, bw-21, th);

    tb_print_custom(bx+2, y+7, th->highlight, th->bg, SYM_DNS);
    tb_print_custom(bx+4, y+7, th->fg, th->bg, "DNS ping   :");
    draw_ping_graph(bx+19, y+7, hist_dns_ping, hist_idx, bw-21, th);

    snprintf(buf, sizeof(buf), "%s %s", SYM_PULSE, state.message);
    tb_print_fixed(bx+2, y+8, th->accent, th->bg, buf, bw-4);

    y += 11;
    tb_hline(bx, y, 0x2508, th->skip, th->bg, bw);
    tb_print_custom(bx+1,  y+1, th->accent|TB_BOLD, th->bg, "[q Quit]");
    tb_print_custom(bx+12, y+1, th->accent|TB_BOLD, th->bg, "[t Themes]");
    tb_print_custom(bx+25, y+1, th->accent|TB_BOLD, th->bg, "[g Graph]");
    tb_print_custom(bx+37, y+1, th->accent|TB_BOLD, th->bg, "[s Spinner]");

    draw_theme_menu(th);
    tb_present();
}

static void print_usage(const char* prog) {
    printf("Usage: %s [OPTIONS]\n\n"
           "Options:\n"
           "  -r, --refresh <seconds>   Refresh interval in seconds (default: 2)\n"
           "  -f, --file    <path>      Path to state file (default: %s)\n"
           "  --no-braille              Use classic block graphs\n"
           "  -h, --help                Show this message\n\n"
           "Keyboard:\n"
           "  q / Ctrl+C   Quit\n"
           "  t            Theme menu\n"
           "  g            Toggle graph style\n"
           "  s            Cycle spinner style\n",
           prog, STATE_FILE_DEFAULT);
}

int main(int argc, char** argv) {
    for (int i = 0; i < HIST_SIZE; i++) {
        hist_mysql[i] = hist_redis[i] = -1;
        hist_lb_ping[i] = hist_nc1_ping[i] = hist_nc2_ping[i] = hist_nfs_ping[i] = hist_dns_ping[i] = -1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-braille") == 0) {
            config.use_braille = 0;
        } else if ((strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--refresh") == 0) && i + 1 < argc) {
            config.display_refresh = atoi(argv[++i]);
            if (config.display_refresh <= 0) {
                fprintf(stderr, "ERROR: refresh must be >0\n");
                return 1;
            }
        } else if ((strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) && i + 1 < argc) {
            strncpy(config.state_file, argv[++i], sizeof(config.state_file)-1);
            config.state_file[sizeof(config.state_file)-1] = '\0';
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "ERROR: unknown argument: %s\n", argv[i]);
            return 1;
        }
    }

    tb_init();
    tb_set_output_mode(TB_OUTPUT_256);
    tb_set_input_mode(TB_INPUT_ESC);

    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);

    struct tb_event ev;
    uint64_t last_update = 0;
    int last_tick = -1;
    int force_redraw = 1;

    load_state();
    last_update = get_time_ms();

    while (keep_running) {
        uint64_t now = get_time_ms();
        int new_tick = (now / 120) % 1000;

        if (now - last_update >= (uint64_t)config.display_refresh * 1000) {
            load_state();
            last_update = now;
            force_redraw = 1;
        }

        if (new_tick != last_tick || force_redraw) {
            if (force_redraw) {
                Theme* th = &themes[current_theme];
                tb_set_clear_attrs(th->fg, th->bg);
                tb_clear();
            }
            draw_ui(new_tick);
            last_tick = new_tick;
            force_redraw = 0;
        }

        uint64_t next_frame = ((now / 120) + 1) * 120;
        uint64_t current = get_time_ms();
        int wait_ms = current >= next_frame ? 1 : (int)(next_frame - current);

        int res = tb_peek_event(&ev, wait_ms);
        if (res == TB_OK) {
            if (ev.type == TB_EVENT_RESIZE) {
                force_redraw = 1;
            } else if (theme_menu_open) {
                if (ev.type == TB_EVENT_KEY) {
                    if      (ev.key == TB_KEY_ARROW_UP)   { theme_menu_sel = (theme_menu_sel - 1 + num_themes) % num_themes; force_redraw = 1; }
                    else if (ev.key == TB_KEY_ARROW_DOWN) { theme_menu_sel = (theme_menu_sel + 1) % num_themes; force_redraw = 1; }
                    else if (ev.key == TB_KEY_ENTER)      { current_theme = theme_menu_sel; theme_menu_open = 0; force_redraw = 1; }
                    else if (ev.key == TB_KEY_ESC || ev.ch == 'q') { theme_menu_open = 0; force_redraw = 1; }
                }
            } else {
                if (ev.type == TB_EVENT_KEY) {
                    if      (ev.ch == 'q' || ev.key == TB_KEY_CTRL_C) break;
                    else if (ev.ch == 'g') { config.use_braille = !config.use_braille; force_redraw = 1; }
                    else if (ev.ch == 't') { theme_menu_sel = current_theme; theme_menu_open = 1; force_redraw = 1; }
                    else if (ev.ch == 's') { config.spinner_style = (config.spinner_style + 1) % 4; force_redraw = 1; }
                }
            }
        }
    }

    tb_shutdown();
    return 0;
}

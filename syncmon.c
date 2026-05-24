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

#define MAX_LINE  1024
#define MAX_VAL   256
#define HIST_SIZE 600

/* Default state file must match daemon default */
#define STATE_FILE_DEFAULT "/var/log/syncmon/syncmon_state.env"

// Themes
typedef struct {
    const char* name;
    uint16_t bg;
    uint16_t fg;
    uint16_t box1;
    uint16_t box2;
    uint16_t highlight;
    uint16_t accent;
    uint16_t ok;
    uint16_t warn;
    uint16_t err;
    uint16_t skip;
} Theme;

Theme themes[] = {
    { "Default",    TB_DEFAULT, TB_DEFAULT, 13,  12,  11  | TB_BOLD, 14  | TB_BOLD, 46  | TB_BOLD, 226 | TB_BOLD, 196 | TB_BOLD, 244 },
    { "Monokai",    235, 252, 197,  81, 148, 208, 148, 208, 197,  81 },
    { "Dracula",    236, 253, 212, 117, 205, 228,  84, 228, 203, 117 },
    { "Nord",       237, 231, 109, 110, 114, 110, 114, 220, 174, 109 },
    { "Gruvbox",    235, 223, 208, 109, 214, 175, 142, 214, 167, 109 },
    { "Cyberpunk",  233, 253, 199,  51, 226,  39,  46, 226, 196, 240 },
    { "Calm",       235, 252,  67,  66, 109, 151, 108, 179, 167, 242 },
    { "White Paper",230, 236, 240, 242,  22,  88,  28, 130, 124, 246 },
    { "Grayscale",  233, 250, 240, 244, 255, 252, 246, 250, 255 | TB_BOLD, 238 }
};

int num_themes    = sizeof(themes) / sizeof(Theme);
int current_theme = 0;
int theme_menu_open = 0;
int theme_menu_sel  = 0;

// Runtime configuration (no external config file needed)
struct {
    int  display_refresh;
    int  test_mode;
    char state_file[MAX_VAL];
    int  dash_w;
    int  panel_l;
    int  panel_r;
    int  use_braille;
} config = {
    .display_refresh = 2,
    .test_mode       = 0,
    .state_file      = STATE_FILE_DEFAULT,
    .dash_w          = 92,
    .panel_l         = 42,
    .panel_r         = 48,
    .use_braille     = 1
};

// Application State
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

    char r_m_status[MAX_VAL];
    char r_s_status[MAX_VAL];
    char r_sync[MAX_VAL];
    char r_det[MAX_VAL];
    char r_chk[MAX_VAL];

    char m_m_ep[MAX_VAL];
    char m_s_ep[MAX_VAL];
    char r_m_ep[MAX_VAL];
    char r_s_ep[MAX_VAL];
} state;

int keep_running = 1;

int hist_mysql[HIST_SIZE];
int hist_redis[HIST_SIZE];
int hist_idx = 0;

void handle_sig(int sig) { (void)sig; keep_running = 0; }

uint64_t get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

void get_env_or(const char* key, const char* def, char* out, size_t sz) {
    const char* val = getenv(key);
    if (val && strlen(val) == 0) val = NULL;
    const char* final_val = val ? val : def;
    if (final_val != out) snprintf(out, sz, "%s", final_val);
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
                val[strlen(val)-1] = '\0';
                val++;
            }
            setenv(key, val, 1);
        }
    }
    fclose(f);
}

int rand_range(int min, int max) { return min + rand() % (max - min + 1); }
const char* rand_status() {
    const char* s[] = {"OK", "WARN", "ERROR"};
    return s[rand() % 3];
}

int status_to_val(const char* status) {
    if (strcmp(status, "OK")    == 0) return 4;
    if (strcmp(status, "WARN")  == 0) return 2;
    if (strcmp(status, "ERROR") == 0) return 0;
    return -1;
}

void load_state() {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char current_time[64];
    strftime(current_time, sizeof(current_time), "%Y-%m-%d %H:%M:%S", &tm);

    if (config.test_mode) {
        const char* p[] = {"3E111111-1111-1111-1111-111111111111", "4F222222-2222-2222-2222-222222222222"};
        const char* base = p[rand() % 2];
        int m_gtid = rand_range(1000, 10999);
        int s_gtid = m_gtid;
        if (rand_range(0, 99) < 30) s_gtid = m_gtid - rand_range(1, 50);

        snprintf(state.m_m_gtid, sizeof(state.m_m_gtid), "%s:%d", base, m_gtid);
        snprintf(state.m_s_gtid, sizeof(state.m_s_gtid), "%s:%d", base, s_gtid);
        strcpy(state.m_sync,       m_gtid != s_gtid ? "WARN" : "OK");
        strcpy(state.overall_status, rand_status());
        strcpy(state.m_m_status,   rand_status());
        strcpy(state.m_s_status,   rand_status());
        strcpy(state.r_m_status,   rand_status());
        strcpy(state.r_s_status,   rand_status());
        strcpy(state.r_sync,       rand_status());

        const char* r_link = strcmp(state.r_sync, "ERROR") == 0 ? "down" : "up";
        snprintf(state.r_det, sizeof(state.r_det), "link=%s io=%d host=172.31.%d.%d",
                 r_link, rand_range(0,14), rand_range(0,254), rand_range(0,254));

        strcpy(state.timestamp, current_time);
        strcpy(state.m_chk,     current_time);
        strcpy(state.r_chk,     current_time);
        strcpy(state.m_m_ep,    "172.31.49.233:3306");
        strcpy(state.m_s_ep,    "172.31.40.234:3306");
        strcpy(state.r_m_ep,    "172.31.40.234:6379");
        strcpy(state.r_s_ep,    "172.31.40.233:6379");
        strcpy(state.message,   "Live Simulation Data Active");
    } else {
        if (access(config.state_file, R_OK) == 0) parse_env_file(config.state_file);

        get_env_or("OVERALL_STATUS",           "NO DATA",             state.overall_status, sizeof(state.overall_status));
        get_env_or("MYSQL_MASTER_STATUS",       "N/A",                 state.m_m_status,     sizeof(state.m_m_status));
        get_env_or("MYSQL_SLAVE_STATUS",        "N/A",                 state.m_s_status,     sizeof(state.m_s_status));
        get_env_or("MYSQL_SYNC_STATUS",         "N/A",                 state.m_sync,         sizeof(state.m_sync));
        get_env_or("MYSQL_MASTER_GTID",         "unknown",             state.m_m_gtid,       sizeof(state.m_m_gtid));
        get_env_or("MYSQL_SLAVE_GTID",          "unknown",             state.m_s_gtid,       sizeof(state.m_s_gtid));
        get_env_or("REDIS_MASTER_STATUS",       "N/A",                 state.r_m_status,     sizeof(state.r_m_status));
        get_env_or("REDIS_SLAVE_STATUS",        "N/A",                 state.r_s_status,     sizeof(state.r_s_status));
        get_env_or("REDIS_REPLICATION_STATUS",  "N/A",                 state.r_sync,         sizeof(state.r_sync));
        get_env_or("REDIS_REPLICATION_DETAIL",  "not available",       state.r_det,          sizeof(state.r_det));
        get_env_or("SYNCMON_MESSAGE",            "Waiting for daemon...", state.message,      sizeof(state.message));
        get_env_or("SYNCMON_TIMESTAMP",          current_time,          state.timestamp,      sizeof(state.timestamp));
        get_env_or("MYSQL_CHECK_TIMESTAMP",      state.timestamp,       state.m_chk,          sizeof(state.m_chk));
        get_env_or("REDIS_CHECK_TIMESTAMP",      state.timestamp,       state.r_chk,          sizeof(state.r_chk));

        char mh[64], mp[64], sh[64], sp[64];
        get_env_or("MYSQL_MASTER_HOST", "unknown", mh, sizeof(mh)); get_env_or("MYSQL_MASTER_PORT", "?", mp, sizeof(mp));
        snprintf(state.m_m_ep, sizeof(state.m_m_ep), "%s:%s", mh, mp);
        get_env_or("MYSQL_SLAVE_HOST",  "unknown", sh, sizeof(sh)); get_env_or("MYSQL_SLAVE_PORT",  "?", sp, sizeof(sp));
        snprintf(state.m_s_ep, sizeof(state.m_s_ep), "%s:%s", sh, sp);
        get_env_or("REDIS_MASTER_HOST", "unknown", mh, sizeof(mh)); get_env_or("REDIS_MASTER_PORT", "?", mp, sizeof(mp));
        snprintf(state.r_m_ep, sizeof(state.r_m_ep), "%s:%s", mh, mp);
        get_env_or("REDIS_SLAVE_HOST",  "unknown", sh, sizeof(sh)); get_env_or("REDIS_SLAVE_PORT",  "?", sp, sizeof(sp));
        snprintf(state.r_s_ep, sizeof(state.r_s_ep), "%s:%s", sh, sp);
    }

    hist_mysql[hist_idx] = status_to_val(state.m_sync);
    hist_redis[hist_idx] = status_to_val(state.r_sync);
    hist_idx = (hist_idx + 1) % HIST_SIZE;
}

// ---------- UI Helpers ----------
int tb_print_custom(int x, int y, uint16_t fg, uint16_t bg, const char *str) {
    while (*str) {
        uint32_t uni;
        str += tb_utf8_char_to_unicode(&uni, str);
        tb_set_cell(x, y, uni, fg, bg);
        x++;
    }
    return 0;
}

void tb_print_fixed(int x, int y, uint16_t fg, uint16_t bg, const char *str, int width) {
    int printed = 0;
    while (*str && printed < width) {
        uint32_t uni;
        str += tb_utf8_char_to_unicode(&uni, str);
        tb_set_cell(x, y, uni, fg, bg);
        x++; printed++;
    }
    while (printed < width) { tb_set_cell(x, y, ' ', fg, bg); x++; printed++; }
}

uint16_t get_status_fg(const char* status, Theme* th) {
    if (strcmp(status, "OK")    == 0) return th->ok;
    if (strcmp(status, "WARN")  == 0) return th->warn;
    if (strcmp(status, "ERROR") == 0) return th->err;
    if (strcmp(status, "SKIPPED") == 0 || strcmp(status, "NO DATA") == 0 || strcmp(status, "N/A") == 0) return th->skip;
    return th->fg;
}

void draw_status_bar(int x, int y, const char* status, Theme* th) {
    int fill = 0;
    if      (strcmp(status, "OK")      == 0) fill = 10;
    else if (strcmp(status, "WARN")    == 0) fill = 6;
    else if (strcmp(status, "ERROR")   == 0) fill = 3;
    else if (strcmp(status, "SKIPPED") == 0) fill = 4;
    else if (strcmp(status, "NO DATA") == 0 || strcmp(status, "N/A") == 0) fill = 2;
    uint16_t fg = get_status_fg(status, th);
    for (int i = 0; i < 10; i++) {
        uint32_t ch = (i < fill) ? 0x2588 : 0x2591;
        uint16_t c_fg = (i < fill) ? fg : th->fg;
        tb_set_cell(x+i, y, ch, c_fg, th->bg);
    }
}

void draw_status_token(int x, int y, const char* status, Theme* th) {
    uint16_t fg = get_status_fg(status, th);
    tb_set_cell(x, y, '[', fg, th->bg);
    tb_print_fixed(x+1, y, fg, th->bg, status, 7);
    tb_set_cell(x+8, y, ']', fg, th->bg);
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
        int tlen = strlen(title);
        int tx = x + (w - tlen - 2) / 2;
        tb_set_cell(tx, y, ' ', th->bg, th->bg);
        tb_print_custom(tx+1, y, th->fg | TB_BOLD, th->bg, title);
        tb_set_cell(tx+tlen+1, y, ' ', th->bg, th->bg);
    }
}

uint32_t braille_bar(int h1, int h2) {
    if (h1 < 0) h1 = 0; if (h1 > 4) h1 = 4;
    if (h2 < 0) h2 = 0; if (h2 > 4) h2 = 4;
    uint32_t base = 0x2800;
    if (h1 >= 1) base |= 0x40; if (h1 >= 2) base |= 0x04;
    if (h1 >= 3) base |= 0x02; if (h1 >= 4) base |= 0x01;
    if (h2 >= 1) base |= 0x80; if (h2 >= 2) base |= 0x20;
    if (h2 >= 3) base |= 0x10; if (h2 >= 4) base |= 0x08;
    return base;
}

void draw_graph(int x, int y, int* hist_data, int head, int max_w, Theme* th) {
    for (int c = 0; c < max_w; c++) {
        int idx = head - 1 - (max_w - 1 - c);
        while (idx < 0) idx += HIST_SIZE;
        idx %= HIST_SIZE;
        int v = hist_data[idx];
        if (v == -1) { tb_set_cell(x+c, y, ' ', th->fg, th->bg); continue; }
        if (v == 0)  { tb_set_cell(x+c, y, 'X', th->bg | TB_BOLD, th->err); continue; }
        uint16_t color = (v == 2) ? th->warn : th->ok;
        if (config.use_braille) {
            tb_set_cell(x+c, y, braille_bar(v, v), color, th->bg);
        } else {
            uint32_t ch = ' ';
            if (v == 1) ch = 0x2582;
            else if (v == 2) ch = 0x2584;
            else if (v == 3) ch = 0x2586;
            else if (v >= 4) ch = 0x2588;
            tb_set_cell(x+c, y, ch, color, th->bg);
        }
    }
}

void draw_theme_menu(Theme* th) {
    if (!theme_menu_open) return;
    int w = 30, h = num_themes + 2;
    int sx = (tb_width()  - w) / 2;
    int sy = (tb_height() - h) / 2;
    for (int i = 0; i < h; i++)
        for (int j = 0; j < w; j++)
            tb_set_cell(sx+j, sy+i, ' ', th->fg, th->bg);
    draw_box(sx, sy, w, h, th->accent, "Select Theme", th);
    for (int i = 0; i < num_themes; i++) {
        if (i == theme_menu_sel)
            tb_print_fixed(sx+2, sy+1+i, th->bg, th->accent, themes[i].name, w-4);
        else
            tb_print_fixed(sx+2, sy+1+i, th->fg, th->bg,     themes[i].name, w-4);
    }
}

void format_shortened_left(char *buf, size_t buf_sz, const char* prefix, const char* val, int max_disp_len) {
    int pref_len = strlen(prefix);
    int val_len  = strlen(val);
    if (pref_len + val_len <= max_disp_len) {
        snprintf(buf, buf_sz, "%s%s", prefix, val);
    } else {
        int keep_len = max_disp_len - pref_len - 3;
        if (keep_len < 0)
            snprintf(buf, buf_sz, "%.*s", max_disp_len, prefix);
        else
            snprintf(buf, buf_sz, "%s...%s", prefix, val + (val_len - keep_len));
    }
}

/*
 * draw_placeholder_panel: renders a generic info panel for components
 * that do not yet have a live data source.
 * Shows: Host, Reachability, Function  -- all as "N/A (not yet configured)"
 */
void draw_placeholder_panel(int x, int y, int w, int h, const char* title, Theme* th) {
    draw_box(x, y, w, h, th->box2, title, th);
    tb_print_custom(x+2, y+1, th->fg,      th->bg, "Host        :");
    tb_print_custom(x+16, y+1, th->skip,   th->bg, "N/A (not yet configured)");
    tb_print_custom(x+2, y+2, th->fg,      th->bg, "Reachability:");
    tb_print_custom(x+16, y+2, th->skip,   th->bg, "N/A");
    draw_status_bar(x+20, y+2, "N/A", th);
    tb_print_custom(x+2, y+3, th->fg,      th->bg, "Function    :");
    tb_print_custom(x+16, y+3, th->skip,   th->bg, "N/A (not yet configured)");
}

void draw_ui(int anim_tick) {
    Theme *th = &themes[current_theme];
    tb_set_clear_attrs(th->fg, th->bg);
    tb_clear();

    int w    = tb_width();
    int bw   = config.dash_w + 2;   /* total dashboard width */
    int bx   = (w - bw) / 2;
    if (bx < 0) bx = 0;

    const char* spinner[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
    char buf[1024];

    /* ---- Row 0: Overview (full width, 6 lines) ---- */
    int y = 1;
    draw_box(bx, y, bw, 6, th->box1, "Overview", th);
    tb_print_custom(bx+2, y+1, th->accent, th->bg, "MariaDB + Redis cluster monitor");
    snprintf(buf, sizeof(buf), "Refresh: %ds  Updated: %s  ", config.display_refresh, state.timestamp);
    tb_print_custom(bx+bw-4-(int)strlen(buf), y+1, th->fg, th->bg, buf);
    tb_print_custom(bx+bw-4, y+1, th->accent, th->bg, spinner[anim_tick % 10]);

    tb_print_custom(bx+2, y+2, th->fg, th->bg, "Overall :");
    draw_status_token(bx+12, y+2, state.overall_status, th);

    snprintf(buf, sizeof(buf), "%s -> %s", state.m_m_ep, state.m_s_ep);
    tb_print_custom(bx+2, y+3, th->highlight, th->bg, "MariaDB:");
    tb_print_custom(bx+11, y+3, th->fg, th->bg, buf);
    draw_status_bar(bx+bw-35, y+3, state.m_m_status, th);
    draw_status_bar(bx+bw-24, y+3, state.m_s_status, th);
    draw_status_bar(bx+bw-13, y+3, state.m_sync, th);

    snprintf(buf, sizeof(buf), "%s -> %s", state.r_m_ep, state.r_s_ep);
    tb_print_custom(bx+2, y+4, th->highlight, th->bg, "Redis  :");
    tb_print_custom(bx+11, y+4, th->fg, th->bg, buf);
    draw_status_bar(bx+bw-35, y+4, state.r_m_status, th);
    draw_status_bar(bx+bw-24, y+4, state.r_s_status, th);
    draw_status_bar(bx+bw-13, y+4, state.r_sync, th);

    /* ---- Row 1: Loadbalancer (left ~66%) + DNS (right ~33%), height 6 ---- */
    y += 7;
    int lb_w  = (bw * 2) / 3;
    int dns_w = bw - lb_w;
    draw_placeholder_panel(bx,        y, lb_w,  6, "Loadbalancer", th);
    draw_placeholder_panel(bx+lb_w,   y, dns_w, 6, "DNS",          th);

    /* ---- Row 2: Nextcloud 1 (left half) + Nextcloud 2 (right half), height 6 ---- */
    y += 7;
    int nc_w = bw / 2;
    int nc2_w = bw - nc_w;
    draw_placeholder_panel(bx,       y, nc_w,  6, "Nextcloud 1", th);
    draw_placeholder_panel(bx+nc_w,  y, nc2_w, 6, "Nextcloud 2", th);

    /* ---- Row 3: NFS (left ~33%) + MariaDB (middle ~33%) + Redis (right ~33%) ---- */
    y += 7;
    int third   = bw / 3;
    int third2  = bw / 3;
    int third3  = bw - third - third2;   /* absorbs rounding */

    /* NFS placeholder */
    draw_placeholder_panel(bx, y, third, 6, "NFS", th);

    /* MariaDB Health (full data) */
    int mx = bx + third;
    draw_box(mx, y, third2, 9, th->box2, "MariaDB", th);
    tb_print_custom(mx+2, y+1, th->fg, th->bg, "Master status :");
    draw_status_token(mx+18, y+1, state.m_m_status, th); draw_status_bar(mx+28, y+1, state.m_m_status, th);
    snprintf(buf, sizeof(buf), "Host: %s", state.m_m_ep);
    tb_print_custom(mx+2, y+2, th->fg, th->bg, buf);
    tb_print_custom(mx+2, y+3, th->fg, th->bg, "Slave status  :");
    draw_status_token(mx+18, y+3, state.m_s_status, th); draw_status_bar(mx+28, y+3, state.m_s_status, th);
    snprintf(buf, sizeof(buf), "Host: %s", state.m_s_ep);
    tb_print_custom(mx+2, y+4, th->fg, th->bg, buf);
    tb_print_custom(mx+2, y+5, th->fg, th->bg, "Sync status   :");
    draw_status_token(mx+18, y+5, state.m_sync, th); draw_status_bar(mx+28, y+5, state.m_sync, th);
    int gtid_max = third2 - 4;
    format_shortened_left(buf, sizeof(buf), "M-GTID: ", state.m_m_gtid, gtid_max);
    tb_print_custom(mx+2, y+6, th->fg, th->bg, buf);
    format_shortened_left(buf, sizeof(buf), "S-GTID: ", state.m_s_gtid, gtid_max);
    tb_print_custom(mx+2, y+7, th->fg, th->bg, buf);

    /* Redis Health (full data) */
    int rx = bx + third + third2;
    draw_box(rx, y, third3, 9, th->box2, "Redis", th);
    tb_print_custom(rx+2, y+1, th->fg, th->bg, "Master status :");
    draw_status_token(rx+18, y+1, state.r_m_status, th); draw_status_bar(rx+28, y+1, state.r_m_status, th);
    snprintf(buf, sizeof(buf), "Host: %s", state.r_m_ep);
    tb_print_custom(rx+2, y+2, th->fg, th->bg, buf);
    tb_print_custom(rx+2, y+3, th->fg, th->bg, "Slave status  :");
    draw_status_token(rx+18, y+3, state.r_s_status, th); draw_status_bar(rx+28, y+3, state.r_s_status, th);
    snprintf(buf, sizeof(buf), "Host: %s", state.r_s_ep);
    tb_print_custom(rx+2, y+4, th->fg, th->bg, buf);
    tb_print_custom(rx+2, y+5, th->fg, th->bg, "Replication   :");
    draw_status_token(rx+18, y+5, state.r_sync, th); draw_status_bar(rx+28, y+5, state.r_sync, th);
    snprintf(buf, sizeof(buf), "Detail: %s", state.r_det);
    tb_print_fixed(rx+2, y+6, th->fg, th->bg, buf, third3-4);
    snprintf(buf, sizeof(buf), "Checked: %s", state.r_chk);
    tb_print_fixed(rx+2, y+7, th->fg, th->bg, buf, third3-4);

    /* ---- Footer: history graphs + message ---- */
    y += 10;
    draw_box(bx, y, bw, 5, th->box1, "History / Details", th);
    tb_print_custom(bx+2, y+1, th->highlight, th->bg, "MariaDB sync :");
    draw_graph(bx+16, y+1, hist_mysql, hist_idx, bw-18-2, th);
    tb_print_custom(bx+2, y+2, th->highlight, th->bg, "Redis  repl  :");
    draw_graph(bx+16, y+2, hist_redis, hist_idx, bw-18-2, th);
    snprintf(buf, sizeof(buf), "Message : %s", state.message);
    tb_print_custom(bx+2, y+3, th->fg, th->bg, buf);

    tb_print_custom(bx, y+6, th->fg, th->bg, "Press 'q' quit | 't' themes | 'g' graph style");

    draw_theme_menu(th);
    tb_present();
}

static void print_usage(const char* prog) {
    printf("Usage: %s [OPTIONS]\n\n"
           "Options:\n"
           "  -r, --refresh <seconds>   Refresh interval in seconds (default: 2)\n"
           "  -f, --file    <path>      Path to state file\n"
           "                            (default: %s)\n"
           "  --test                    Run with simulated mock data\n"
           "  --no-braille              Use classic block graphs\n"
           "  -h, --help                Show this message\n\n"
           "Keyboard:\n"
           "  q / Ctrl+C  Quit\n"
           "  t           Theme menu (navigate with arrows + Enter)\n"
           "  g           Toggle graph style\n",
           prog, STATE_FILE_DEFAULT);
}

int main(int argc, char** argv) {
    for (int i = 0; i < HIST_SIZE; i++) { hist_mysql[i] = -1; hist_redis[i] = -1; }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test") == 0) {
            config.test_mode = 1;
        } else if (strcmp(argv[i], "--no-braille") == 0) {
            config.use_braille = 0;
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--refresh") == 0) {
            if (i + 1 < argc) {
                config.display_refresh = atoi(argv[++i]);
                if (config.display_refresh <= 0) { fprintf(stderr, "ERROR: refresh must be > 0\n"); return 1; }
            } else { fprintf(stderr, "ERROR: %s requires an integer argument\n", argv[i]); return 1; }
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) {
            if (i + 1 < argc) {
                strncpy(config.state_file, argv[++i], sizeof(config.state_file)-1);
                config.state_file[sizeof(config.state_file)-1] = '\0';
            } else { fprintf(stderr, "ERROR: %s requires a path argument\n", argv[i]); return 1; }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "ERROR: unknown argument: %s\n", argv[i]);
            return 1;
        }
    }

    if (config.display_refresh <= 0) config.display_refresh = 2;

    srand(time(NULL));
    tb_init();
    tb_set_output_mode(TB_OUTPUT_256);
    tb_set_input_mode(TB_INPUT_ESC);

    signal(SIGINT,  handle_sig);
    signal(SIGTERM, handle_sig);

    struct tb_event ev;
    uint64_t last_update = 0;
    int anim_tick = 0;

    load_state();
    last_update = get_time_ms();

    while (keep_running) {
        uint64_t now = get_time_ms();
        if (now - last_update >= (uint64_t)config.display_refresh * 1000) {
            load_state();
            last_update = now;
        }
        anim_tick = (now / 100) % 10;
        draw_ui(anim_tick);

        int res = tb_peek_event(&ev, 100);
        if (res == TB_OK) {
            if (theme_menu_open) {
                if (ev.type == TB_EVENT_KEY) {
                    if      (ev.key == TB_KEY_ARROW_UP)   theme_menu_sel = (theme_menu_sel - 1 + num_themes) % num_themes;
                    else if (ev.key == TB_KEY_ARROW_DOWN) theme_menu_sel = (theme_menu_sel + 1) % num_themes;
                    else if (ev.key == TB_KEY_ENTER)      { current_theme = theme_menu_sel; theme_menu_open = 0; }
                    else if (ev.key == TB_KEY_ESC || ev.ch == 'q') theme_menu_open = 0;
                }
            } else {
                if (ev.type == TB_EVENT_KEY) {
                    if      (ev.ch == 'q' || ev.key == TB_KEY_CTRL_C) break;
                    else if (ev.ch == 'g') config.use_braille = !config.use_braille;
                    else if (ev.ch == 't') { theme_menu_sel = current_theme; theme_menu_open = 1; }
                }
            }
        }
    }

    tb_shutdown();
    return 0;
}

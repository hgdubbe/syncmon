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

#define STATE_FILE_DEFAULT "/var/log/syncmon/syncmon_state.env"

typedef struct {
    const char* name;
    uint16_t bg, fg, box1, box2, highlight, accent, ok, warn, err, skip;
} Theme;

Theme themes[] = {
    { "Default",     TB_DEFAULT, TB_DEFAULT, 13,  12,  11|TB_BOLD, 14|TB_BOLD, 46|TB_BOLD, 226|TB_BOLD, 196|TB_BOLD, 244 },
    { "Monokai",     235, 252, 197,  81, 148, 208, 148, 208, 197,  81 },
    { "Dracula",     236, 253, 212, 117, 205, 228,  84, 228, 203, 117 },
    { "Nord",        237, 231, 109, 110, 114, 110, 114, 220, 174, 109 },
    { "Gruvbox",     235, 223, 208, 109, 214, 175, 142, 214, 167, 109 },
    { "Cyberpunk",   233, 253, 199,  51, 226,  39,  46, 226, 196, 240 },
    { "Calm",        235, 252,  67,  66, 109, 151, 108, 179, 167, 242 },
    { "White Paper", 230, 236, 240, 242,  22,  88,  28, 130, 124, 246 },
    { "Grayscale",   233, 250, 240, 244, 255, 252, 246, 250, 255|TB_BOLD, 238 }
};

int num_themes    = sizeof(themes) / sizeof(Theme);
int current_theme = 0;
int theme_menu_open = 0;
int theme_menu_sel  = 0;

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

struct {
    /* overview */
    char overall_status[MAX_VAL];
    char timestamp[MAX_VAL];
    char message[MAX_VAL];
    /* mariadb */
    char m_m_status[MAX_VAL];  char m_s_status[MAX_VAL];
    char m_sync[MAX_VAL];      char m_m_gtid[MAX_VAL];  char m_s_gtid[MAX_VAL];
    char m_chk[MAX_VAL];       char m_m_ep[MAX_VAL];    char m_s_ep[MAX_VAL];
    /* redis */
    char r_m_status[MAX_VAL];  char r_s_status[MAX_VAL];
    char r_sync[MAX_VAL];      char r_det[MAX_VAL];     char r_chk[MAX_VAL];
    char r_m_ep[MAX_VAL];      char r_s_ep[MAX_VAL];
    /* loadbalancer */
    char lb_host[MAX_VAL];     char lb_ping[MAX_VAL];   char lb_ping_status[MAX_VAL];
    char lb_check[MAX_VAL];    char lb_chk[MAX_VAL];
    /* dns */
    char dns_host[MAX_VAL];    char dns_ping[MAX_VAL];  char dns_ping_status[MAX_VAL];
    char dns_check[MAX_VAL];   char dns_chk[MAX_VAL];
    /* nextcloud 1 */
    char nc1_host[MAX_VAL];    char nc1_ping[MAX_VAL];  char nc1_ping_status[MAX_VAL];
    char nc1_check[MAX_VAL];   char nc1_chk[MAX_VAL];
    /* nextcloud 2 */
    char nc2_host[MAX_VAL];    char nc2_ping[MAX_VAL];  char nc2_ping_status[MAX_VAL];
    char nc2_check[MAX_VAL];   char nc2_chk[MAX_VAL];
    /* nfs */
    char nfs_host[MAX_VAL];    char nfs_ping[MAX_VAL];  char nfs_ping_status[MAX_VAL];
    char nfs_check[MAX_VAL];   char nfs_chk[MAX_VAL];
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
const char* rand_status() { const char* s[] = {"OK","WARN","ERROR"}; return s[rand()%3]; }

int status_to_val(const char* s) {
    if (strcmp(s,"OK")==0)    return 4;
    if (strcmp(s,"WARN")==0)  return 2;
    if (strcmp(s,"ERROR")==0) return 0;
    return -1;
}

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

        /* mock ping: "OK  3ms", "WARN  251ms", "ERROR  timeout" */
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
        get_env_or("OVERALL_STATUS",          "NO DATA",              state.overall_status, sizeof(state.overall_status));
        get_env_or("MYSQL_MASTER_STATUS",      "N/A",                  state.m_m_status,     sizeof(state.m_m_status));
        get_env_or("MYSQL_SLAVE_STATUS",       "N/A",                  state.m_s_status,     sizeof(state.m_s_status));
        get_env_or("MYSQL_SYNC_STATUS",        "N/A",                  state.m_sync,         sizeof(state.m_sync));
        get_env_or("MYSQL_MASTER_GTID",        "unknown",              state.m_m_gtid,       sizeof(state.m_m_gtid));
        get_env_or("MYSQL_SLAVE_GTID",         "unknown",              state.m_s_gtid,       sizeof(state.m_s_gtid));
        get_env_or("REDIS_MASTER_STATUS",      "N/A",                  state.r_m_status,     sizeof(state.r_m_status));
        get_env_or("REDIS_SLAVE_STATUS",       "N/A",                  state.r_s_status,     sizeof(state.r_s_status));
        get_env_or("REDIS_REPLICATION_STATUS", "N/A",                  state.r_sync,         sizeof(state.r_sync));
        get_env_or("REDIS_REPLICATION_DETAIL", "not available",        state.r_det,          sizeof(state.r_det));
        get_env_or("SYNCMON_MESSAGE",          "Waiting for daemon...",state.message,        sizeof(state.message));
        get_env_or("SYNCMON_TIMESTAMP",        now,                    state.timestamp,      sizeof(state.timestamp));
        get_env_or("MYSQL_CHECK_TIMESTAMP",    state.timestamp,        state.m_chk,          sizeof(state.m_chk));
        get_env_or("REDIS_CHECK_TIMESTAMP",    state.timestamp,        state.r_chk,          sizeof(state.r_chk));
        char mh[64],mp[64],sh[64],sp[64];
        get_env_or("MYSQL_MASTER_HOST","unknown",mh,sizeof(mh)); get_env_or("MYSQL_MASTER_PORT","?",mp,sizeof(mp));
        snprintf(state.m_m_ep, sizeof(state.m_m_ep), "%s:%s", mh, mp);
        get_env_or("MYSQL_SLAVE_HOST", "unknown",sh,sizeof(sh)); get_env_or("MYSQL_SLAVE_PORT", "?",sp,sizeof(sp));
        snprintf(state.m_s_ep, sizeof(state.m_s_ep), "%s:%s", sh, sp);
        get_env_or("REDIS_MASTER_HOST","unknown",mh,sizeof(mh)); get_env_or("REDIS_MASTER_PORT","?",mp,sizeof(mp));
        snprintf(state.r_m_ep, sizeof(state.r_m_ep), "%s:%s", mh, mp);
        get_env_or("REDIS_SLAVE_HOST", "unknown",sh,sizeof(sh)); get_env_or("REDIS_SLAVE_PORT", "?",sp,sizeof(sp));
        snprintf(state.r_s_ep, sizeof(state.r_s_ep), "%s:%s", sh, sp);
        /* placeholder panels */
#define LOAD_COMP(pfx, KEY) \
        get_env_or(KEY"_HOST",         "N/A", state.pfx##_host,        sizeof(state.pfx##_host)); \
        get_env_or(KEY"_PING",         "N/A", state.pfx##_ping,        sizeof(state.pfx##_ping)); \
        get_env_or(KEY"_PING_STATUS",  "N/A", state.pfx##_ping_status, sizeof(state.pfx##_ping_status)); \
        get_env_or(KEY"_CHECK",        "N/A", state.pfx##_check,       sizeof(state.pfx##_check)); \
        get_env_or(KEY"_CHECK_TIMESTAMP", "never", state.pfx##_chk,   sizeof(state.pfx##_chk));
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
}

/* ── UI helpers ─────────────────────────────────────────────────────────── */
int tb_print_custom(int x, int y, uint16_t fg, uint16_t bg, const char *str) {
    while (*str) { uint32_t u; str += tb_utf8_char_to_unicode(&u, str); tb_set_cell(x++,y,u,fg,bg); }
    return 0;
}
void tb_print_fixed(int x, int y, uint16_t fg, uint16_t bg, const char *str, int w) {
    int p=0;
    while (*str && p<w) { uint32_t u; str+=tb_utf8_char_to_unicode(&u,str); tb_set_cell(x++,y,u,fg,bg); p++; }
    while (p++<w) tb_set_cell(x++,y,' ',fg,bg);
}
uint16_t get_status_fg(const char* s, Theme* th) {
    if (strcmp(s,"OK")==0)    return th->ok;
    if (strcmp(s,"WARN")==0)  return th->warn;
    if (strcmp(s,"ERROR")==0) return th->err;
    return th->skip;
}
void draw_status_bar(int x, int y, const char* s, Theme* th) {
    int fill=0;
    if      (strcmp(s,"OK")==0)    fill=10;
    else if (strcmp(s,"WARN")==0)  fill=6;
    else if (strcmp(s,"ERROR")==0) fill=3;
    else if (strcmp(s,"N/A")==0)   fill=2;
    uint16_t fg = get_status_fg(s,th);
    for (int i=0;i<10;i++) {
        uint32_t ch=(i<fill)?0x2588:0x2591;
        tb_set_cell(x+i,y,ch,(i<fill)?fg:th->fg,th->bg);
    }
}
void draw_status_token(int x, int y, const char* s, Theme* th) {
    uint16_t fg=get_status_fg(s,th);
    tb_set_cell(x,y,'[',fg,th->bg);
    tb_print_fixed(x+1,y,fg,th->bg,s,7);
    tb_set_cell(x+8,y,']',fg,th->bg);
}
void draw_box(int x, int y, int w, int h, uint16_t fg, const char* title, Theme* th) {
    tb_set_cell(x,    y,    0x256D,fg,th->bg); tb_set_cell(x+w-1,y,    0x256E,fg,th->bg);
    tb_set_cell(x,    y+h-1,0x2570,fg,th->bg); tb_set_cell(x+w-1,y+h-1,0x256F,fg,th->bg);
    for (int i=1;i<w-1;i++) { tb_set_cell(x+i,y,0x2500,fg,th->bg); tb_set_cell(x+i,y+h-1,0x2500,fg,th->bg); }
    for (int i=1;i<h-1;i++) { tb_set_cell(x,y+i,0x2502,fg,th->bg); tb_set_cell(x+w-1,y+i,0x2502,fg,th->bg); }
    if (title) {
        int tl=strlen(title), tx=x+(w-tl-2)/2;
        tb_set_cell(tx,y,' ',th->bg,th->bg);
        tb_print_custom(tx+1,y,th->fg|TB_BOLD,th->bg,title);
        tb_set_cell(tx+tl+1,y,' ',th->bg,th->bg);
    }
}
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
        if(v==-1){tb_set_cell(x+c,y,' ',th->fg,th->bg);continue;}
        if(v==0) {tb_set_cell(x+c,y,'X',th->bg|TB_BOLD,th->err);continue;}
        uint16_t col=(v==2)?th->warn:th->ok;
        if(config.use_braille) tb_set_cell(x+c,y,braille_bar(v,v),col,th->bg);
        else { uint32_t ch=' '; if(v==1)ch=0x2582;else if(v==2)ch=0x2584;else if(v==3)ch=0x2586;else ch=0x2588; tb_set_cell(x+c,y,ch,col,th->bg); }
    }
}
void draw_theme_menu(Theme* th) {
    if (!theme_menu_open) return;
    int w=30, h=num_themes+2, sx=(tb_width()-w)/2, sy=(tb_height()-h)/2;
    for(int i=0;i<h;i++) for(int j=0;j<w;j++) tb_set_cell(sx+j,sy+i,' ',th->fg,th->bg);
    draw_box(sx,sy,w,h,th->accent,"Select Theme",th);
    for(int i=0;i<num_themes;i++)
        tb_print_fixed(sx+2,sy+1+i,(i==theme_menu_sel)?th->bg:th->fg,(i==theme_menu_sel)?th->accent:th->bg,themes[i].name,w-4);
}
void format_shortened_left(char *buf, size_t bsz, const char* prefix, const char* val, int maxlen) {
    int pl=strlen(prefix), vl=strlen(val);
    if(pl+vl<=maxlen) snprintf(buf,bsz,"%s%s",prefix,val);
    else { int kl=maxlen-pl-3; if(kl<0) snprintf(buf,bsz,"%.*s",maxlen,prefix); else snprintf(buf,bsz,"%s...%s",prefix,val+(vl-kl)); }
}

/*
 * draw_simple_panel  h=7
 *   row 1: Ping     : [OK     ] ████████░░   OK  3ms
 *   row 2: Host     : 172.31.x.x
 *   row 3: Check    : <function test result>
 *   row 4: Checked  : 2026-05-24 14:18:00
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
    /* row 1: ping status token + bar + latency string */
    tb_print_custom(x+2, y+1, th->fg, th->bg, "Ping     :");
    draw_status_token(x+13, y+1, ping_status, th);
    draw_status_bar  (x+23, y+1, ping_status, th);
    tb_print_fixed   (x+34, y+1, get_status_fg(ping_status,th), th->bg, ping, w-36);
    /* row 2: host */
    char buf[MAX_VAL];
    snprintf(buf, sizeof(buf), "Host     : %s", host);
    tb_print_fixed(x+2, y+2, th->fg, th->bg, buf, w-4);
    /* row 3: function check result */
    snprintf(buf, sizeof(buf), "Check    : %s", check);
    tb_print_fixed(x+2, y+3, th->fg, th->bg, buf, w-4);
    /* row 4: checked timestamp */
    snprintf(buf, sizeof(buf), "Checked  : %s", chk_ts);
    tb_print_fixed(x+2, y+4, th->fg, th->bg, buf, w-4);
    (void)h;
}

void draw_ui(int anim_tick) {
    Theme *th = &themes[current_theme];
    tb_set_clear_attrs(th->fg, th->bg);
    tb_clear();

    int w  = tb_width();
    int bw = config.dash_w + 2;
    int bx = (w - bw) / 2; if (bx < 0) bx = 0;

    const char* spinner[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
    char buf[1024];

    /* ── Row 0: Overview (full width, h=6) ───────────────────────────────── */
    int y = 1;
    draw_box(bx, y, bw, 6, th->box1, "Overview", th);
    tb_print_custom(bx+2, y+1, th->accent, th->bg, "Nextcloud HA cluster monitor");
    snprintf(buf, sizeof(buf), "Refresh: %ds  Updated: %s  ", config.display_refresh, state.timestamp);
    tb_print_custom(bx+bw-4-(int)strlen(buf), y+1, th->fg, th->bg, buf);
    tb_print_custom(bx+bw-4, y+1, th->accent, th->bg, spinner[anim_tick%10]);

    tb_print_custom(bx+2, y+2, th->fg, th->bg, "Overall  :");
    draw_status_token(bx+13, y+2, state.overall_status, th);

    snprintf(buf, sizeof(buf), "%s -> %s", state.m_m_ep, state.m_s_ep);
    tb_print_custom(bx+2, y+3, th->highlight, th->bg, "MariaDB  :");
    tb_print_custom(bx+13, y+3, th->fg, th->bg, buf);
    draw_status_bar(bx+bw-35, y+3, state.m_m_status, th);
    draw_status_bar(bx+bw-24, y+3, state.m_s_status, th);
    draw_status_bar(bx+bw-13, y+3, state.m_sync, th);

    snprintf(buf, sizeof(buf), "%s -> %s", state.r_m_ep, state.r_s_ep);
    tb_print_custom(bx+2, y+4, th->highlight, th->bg, "Redis    :");
    tb_print_custom(bx+13, y+4, th->fg, th->bg, buf);
    draw_status_bar(bx+bw-35, y+4, state.r_m_status, th);
    draw_status_bar(bx+bw-24, y+4, state.r_s_status, th);
    draw_status_bar(bx+bw-13, y+4, state.r_sync, th);

    /* ── Row 1: Loadbalancer (~66%) + DNS (~33%), h=7 ────────────────────── */
    y += 7;
    int lb_w  = (bw * 2) / 3;
    int dns_w = bw - lb_w;
    draw_simple_panel(bx,      y, lb_w,  7, "Loadbalancer",
                      state.lb_host,  state.lb_ping_status, state.lb_ping,
                      state.lb_check, state.lb_chk, th);
    draw_simple_panel(bx+lb_w, y, dns_w, 7, "DNS",
                      state.dns_host, state.dns_ping_status, state.dns_ping,
                      state.dns_check, state.dns_chk, th);

    /* ── Row 2: Nextcloud 1 (50%) + Nextcloud 2 (50%), h=7 ──────────────── */
    y += 8;
    int nc_w  = bw / 2;
    int nc2_w = bw - nc_w;
    draw_simple_panel(bx,      y, nc_w,  7, "Nextcloud 1",
                      state.nc1_host, state.nc1_ping_status, state.nc1_ping,
                      state.nc1_check, state.nc1_chk, th);
    draw_simple_panel(bx+nc_w, y, nc2_w, 7, "Nextcloud 2",
                      state.nc2_host, state.nc2_ping_status, state.nc2_ping,
                      state.nc2_check, state.nc2_chk, th);

    /* ── Row 3: NFS (33%) + MariaDB (33%) + Redis (33%), h=9 ────────────── */
    y += 8;
    int third  = bw / 3;
    int third2 = bw / 3;
    int third3 = bw - third - third2;

    /* NFS */
    draw_simple_panel(bx, y, third, 7, "NFS",
                      state.nfs_host, state.nfs_ping_status, state.nfs_ping,
                      state.nfs_check, state.nfs_chk, th);

    /* MariaDB */
    int mx = bx + third;
    draw_box(mx, y, third2, 9, th->box2, "MariaDB", th);
    tb_print_custom(mx+2, y+1, th->fg, th->bg, "Master status :");
    draw_status_token(mx+18, y+1, state.m_m_status, th);
    draw_status_bar  (mx+28, y+1, state.m_m_status, th);
    snprintf(buf, sizeof(buf), "Host: %s", state.m_m_ep);
    tb_print_fixed(mx+2, y+2, th->fg, th->bg, buf, third2-4);
    tb_print_custom(mx+2, y+3, th->fg, th->bg, "Slave status  :");
    draw_status_token(mx+18, y+3, state.m_s_status, th);
    draw_status_bar  (mx+28, y+3, state.m_s_status, th);
    snprintf(buf, sizeof(buf), "Host: %s", state.m_s_ep);
    tb_print_fixed(mx+2, y+4, th->fg, th->bg, buf, third2-4);
    tb_print_custom(mx+2, y+5, th->fg, th->bg, "Sync status   :");
    draw_status_token(mx+18, y+5, state.m_sync, th);
    draw_status_bar  (mx+28, y+5, state.m_sync, th);
    int gtid_max = third2 - 4;
    format_shortened_left(buf, sizeof(buf), "M-GTID: ", state.m_m_gtid, gtid_max);
    tb_print_fixed(mx+2, y+6, th->fg, th->bg, buf, third2-4);
    format_shortened_left(buf, sizeof(buf), "S-GTID: ", state.m_s_gtid, gtid_max);
    tb_print_fixed(mx+2, y+7, th->fg, th->bg, buf, third2-4);

    /* Redis */
    int rx = bx + third + third2;
    draw_box(rx, y, third3, 9, th->box2, "Redis", th);
    tb_print_custom(rx+2, y+1, th->fg, th->bg, "Master status :");
    draw_status_token(rx+18, y+1, state.r_m_status, th);
    draw_status_bar  (rx+28, y+1, state.r_m_status, th);
    snprintf(buf, sizeof(buf), "Host: %s", state.r_m_ep);
    tb_print_fixed(rx+2, y+2, th->fg, th->bg, buf, third3-4);
    tb_print_custom(rx+2, y+3, th->fg, th->bg, "Slave status  :");
    draw_status_token(rx+18, y+3, state.r_s_status, th);
    draw_status_bar  (rx+28, y+3, state.r_s_status, th);
    snprintf(buf, sizeof(buf), "Host: %s", state.r_s_ep);
    tb_print_fixed(rx+2, y+4, th->fg, th->bg, buf, third3-4);
    tb_print_custom(rx+2, y+5, th->fg, th->bg, "Replication   :");
    draw_status_token(rx+18, y+5, state.r_sync, th);
    draw_status_bar  (rx+28, y+5, state.r_sync, th);
    snprintf(buf, sizeof(buf), "Detail: %s", state.r_det);
    tb_print_fixed(rx+2, y+6, th->fg, th->bg, buf, third3-4);
    snprintf(buf, sizeof(buf), "Checked: %s", state.r_chk);
    tb_print_fixed(rx+2, y+7, th->fg, th->bg, buf, third3-4);

    /* ── Footer: history + message ───────────────────────────────────────── */
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
           "  -f, --file    <path>      Path to state file (default: %s)\n"
           "  --test                    Run with simulated mock data\n"
           "  --no-braille              Use classic block graphs\n"
           "  -h, --help                Show this message\n\n"
           "Keyboard:\n"
           "  q / Ctrl+C  Quit\n"
           "  t           Theme menu (arrows + Enter)\n"
           "  g           Toggle graph style\n",
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
    tb_init(); tb_set_output_mode(TB_OUTPUT_256); tb_set_input_mode(TB_INPUT_ESC);
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
                    if     (ev.key==TB_KEY_ARROW_UP)  theme_menu_sel=(theme_menu_sel-1+num_themes)%num_themes;
                    else if(ev.key==TB_KEY_ARROW_DOWN)theme_menu_sel=(theme_menu_sel+1)%num_themes;
                    else if(ev.key==TB_KEY_ENTER)     {current_theme=theme_menu_sel;theme_menu_open=0;}
                    else if(ev.key==TB_KEY_ESC||ev.ch=='q')theme_menu_open=0;
                }
            } else {
                if(ev.type==TB_EVENT_KEY) {
                    if     (ev.ch=='q'||ev.key==TB_KEY_CTRL_C)break;
                    else if(ev.ch=='g')config.use_braille=!config.use_braille;
                    else if(ev.ch=='t'){theme_menu_sel=current_theme;theme_menu_open=1;}
                }
            }
        }
    }
    tb_shutdown(); return 0;
}

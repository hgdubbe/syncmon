/*
 * syncmon.c  ─  Nextcloud HA Cluster Monitor  [new-gui branch]
 *
 * Layout (per infrastructure mockup):
 *
 *  ╔══════════════════════════════════════════════════════════╗
 *  ║  ✶ SYNCMON ✶   Nextcloud HA Cluster Monitor             ║
 *  ╚══════════════════════════════════════════════════════════╝
 *  ╭──────────────────────────────────────────────────────────╮
 *  │  Overview  │  Overall status  │  MariaDB bars  │ Redis   │
 *  │  ✶ AI Analysis lines (3 rows)                           │
 *  ╰──────────────────────────────────────────────────────────╯
 *  ╭──────────────────────────────────────────────────────────╮
 *  │                     Loadbalancer                         │
 *  ╰──────────────────────────────────────────────────────────╯
 *  ╭───────────────────────────┬──────────────────────────────╮
 *  │      Nextcloud 1          │         Nextcloud 2          │
 *  ╰───────────────────────────┴──────────────────────────────╯
 *  ╭───────────────────────────┬──────────────────────────────╮
 *  │  MariaDB Master           │        MariaDB Slave         │
 *  │                    ┌────────────┐                        │
 *  │  ══════════════>   │ M-GTID:... │   (animated arrow)     │
 *  │                    │ S-GTID:... │                        │
 *  │                    └────────────┘                        │
 *  ╰───────────────────────────┴──────────────────────────────╯
 *  ╭───────────────────────────┬──────────────────────────────╮
 *  │  Redis Master             │        Redis Slave           │
 *  │                    ┌────────────┐                        │
 *  │  ══════════════<   │ Sent: ...  │   (animated arrow)     │
 *  │                    │ Recv: ...  │                        │
 *  │                    └────────────┘                        │
 *  ╰───────────────────────────┴──────────────────────────────╯
 *  ╭───────────────────────────┬──────────────────────────────╮
 *  │        NFS                │           DNS                │
 *  ╰───────────────────────────┴──────────────────────────────╯
 *  ╭──────────────────────────────────────────────────────────╮
 *  │  ⏧ History  MariaDB: ▁▃▇█▇  Redis: ▁▁▇█▇   Message:... │
 *  ╰──────────────────────────────────────────────────────────╯
 *  [ q Quit ] [ t Themes ] [ g Graph ] [ s Spinner ] [ a AI ]
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

#define MAX_LINE  1024
#define MAX_VAL   256
#define HIST_SIZE 600

#define STATE_FILE_DEFAULT "/var/log/syncmon/syncmon_state.env"

/* ── Unicode symbols ──────────────────────────────────────────────────────── */
#define SYM_OK      "\u2714"   /* ✔ */
#define SYM_WARN    "\u26A0"   /* ⚠ */
#define SYM_ERR     "\u2718"   /* ✘ */
#define SYM_SKIP    "\u25CB"   /* ○ */
#define SYM_BULLET  "\u25CF"   /* ● */
#define SYM_AI      "\u2736"   /* ✶ */
#define SYM_DB      "\u25BC"   /* ▼ */
#define SYM_REDIS   "\u25B6"   /* ▶ */
#define SYM_LB      "\u2248"   /* ≈ */
#define SYM_DNS     "\u2318"   /* ⌘ */
#define SYM_NC      "\u2601"   /* ☁ */
#define SYM_NFS     "\u25A0"   /* ■ */
#define SYM_HISTORY "\u29D7"   /* ⧗ */
#define SYM_PULSE   "\u25C9"   /* ◉ */
#define SYM_LINK    "\u21C4"   /* ⇄ */
#define SYM_MASTER  "\u2605"   /* ★ */
#define SYM_SLAVE   "\u2606"   /* ☆ */
#define SYM_CLOCK   "\u29D6"   /* ⧖ */
#define SYM_HOST    "\u25BA"   /* ► */
#define SYM_ARROW_R "\u27A4"   /* ➤ right */
#define SYM_ARROW_L "\u2B9C"   /* ⮜ left  */

/* ── Spinner sets ─────────────────────────────────────────────────────────── */
static const char* SPIN_BRAILLE[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
static const char* SPIN_DOTS[]    = {"⣾","⣽","⣻","⢿","⡿","⣟","⣯","⣷","⣾","⣽"};
static const char* SPIN_PULSE[]   = {"◉","◎","○","◎","◉","●","◉","◎","○","◎"};
static const char* SPIN_ARROW[]   = {"←","↖","↑","↗","→","↘","↓","↙","←","↖"};

/* Animated sync arrows for replication panels */
/* 8-frame sequences that "move" a dot across */
static const char* ANIM_R[8] = {
    "        \u27A4",
    "   \u00B7    \u27A4",
    "   \u00B7\u00B7   \u27A4",
    "   \u00B7\u00B7\u00B7  \u27A4",
    "  \u2550\u2550\u2550\u2550\u2550\u2550\u27A4",
    "\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u27A4",
    " \u2550\u2550\u2550\u2550\u2550\u2550\u2550\u27A4",
    "  \u2550\u2550\u2550\u2550\u2550\u2550\u27A4",
};
static const char* ANIM_L[8] = {
    "\u2B9C        ",
    "\u2B9C  \u00B7     ",
    "\u2B9C  \u00B7\u00B7    ",
    "\u2B9C  \u00B7\u00B7\u00B7   ",
    "\u2B9C\u2550\u2550\u2550\u2550\u2550\u2550  ",
    "\u2B9C\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550",
    "\u2B9C\u2550\u2550\u2550\u2550\u2550\u2550\u2550 ",
    "\u2B9C\u2550\u2550\u2550\u2550\u2550\u2550  ",
};

#define BAR_FULL  0x2588
#define BAR_LOW   0x2591

/* ── Theme ────────────────────────────────────────────────────────────────── */
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
    { "Lava",        233,223, 202,214, 220,196, 46,214,196,244,  232,202,235 },
};
int num_themes    = sizeof(themes)/sizeof(Theme);
int current_theme = 0;
int theme_menu_open = 0;
int theme_menu_sel  = 0;

/* ── Config ───────────────────────────────────────────────────────────────── */
struct {
    int  display_refresh;
    char state_file[MAX_VAL];
    int  dash_w;
    int  use_braille;
    int  spinner_style;
    int  show_ai;
} config = {
    .display_refresh = 2,
    .state_file      = STATE_FILE_DEFAULT,
    .dash_w          = 92,
    .use_braille     = 1,
    .spinner_style   = 0,
    .show_ai         = 1,
};

/* ── State ────────────────────────────────────────────────────────────────── */
struct {
    char overall_status[MAX_VAL];
    char timestamp[MAX_VAL];
    char message[MAX_VAL];
    char m_m_status[MAX_VAL]; char m_s_status[MAX_VAL];
    char m_sync[MAX_VAL];     char m_m_gtid[MAX_VAL]; char m_s_gtid[MAX_VAL];
    char m_chk[MAX_VAL];      char m_m_ep[MAX_VAL];   char m_s_ep[MAX_VAL];
    char r_m_status[MAX_VAL]; char r_s_status[MAX_VAL];
    char r_sync[MAX_VAL];     char r_det[MAX_VAL];    char r_chk[MAX_VAL];
    char r_m_ep[MAX_VAL];     char r_s_ep[MAX_VAL];
    char lb_host[MAX_VAL];    char lb_ping[MAX_VAL];  char lb_ping_status[MAX_VAL];
    char lb_check[MAX_VAL];   char lb_chk[MAX_VAL];
    char dns_host[MAX_VAL];   char dns_ping[MAX_VAL]; char dns_ping_status[MAX_VAL];
    char dns_check[MAX_VAL];  char dns_chk[MAX_VAL];
    char nc1_host[MAX_VAL];   char nc1_ping[MAX_VAL]; char nc1_ping_status[MAX_VAL];
    char nc1_check[MAX_VAL];  char nc1_chk[MAX_VAL];
    char nc2_host[MAX_VAL];   char nc2_ping[MAX_VAL]; char nc2_ping_status[MAX_VAL];
    char nc2_check[MAX_VAL];  char nc2_chk[MAX_VAL];
    char nfs_host[MAX_VAL];   char nfs_ping[MAX_VAL]; char nfs_ping_status[MAX_VAL];
    char nfs_check[MAX_VAL];  char nfs_chk[MAX_VAL];
} state;

int keep_running = 1;
int hist_mysql[HIST_SIZE];
int hist_redis[HIST_SIZE];
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
            if ((val[0]=='"'||val[0]=='\'') && val[strlen(val)-1]==val[0]) {
                val[strlen(val)-1]='\0'; val++;
            }
            setenv(key, val, 1);
        }
    }
    fclose(f);
}

int status_to_val(const char* s) {
    if (strcmp(s,"OK")==0)    return 4;
    if (strcmp(s,"WARN")==0)  return 2;
    if (strcmp(s,"ERROR")==0) return 0;
    return -1;
}

static void derive_check_status(const char* check, char* out, size_t sz) {
    if (!check || strlen(check)==0 || strcmp(check,"N/A")==0) { snprintf(out,sz,"N/A"); return; }
    if (strncmp(check,"ERROR",5)==0)            { snprintf(out,sz,"ERROR"); return; }
    if (strstr(check,"unreachable"))             { snprintf(out,sz,"ERROR"); return; }
    if (strstr(check,"timeout"))                 { snprintf(out,sz,"ERROR"); return; }
    if (strstr(check,"NXDOMAIN"))                { snprintf(out,sz,"ERROR"); return; }
    if (strstr(check," 503")||strstr(check," 502")||
        strstr(check," 500")||strstr(check," 504")) { snprintf(out,sz,"ERROR"); return; }
    if (strstr(check,"link=down"))               { snprintf(out,sz,"ERROR"); return; }
    if (strstr(check,"no exports"))              { snprintf(out,sz,"ERROR"); return; }
    if (strstr(check,"WARN")||strstr(check,"WARNING")) { snprintf(out,sz,"WARN"); return; }
    if (strstr(check,"maintenance=true"))        { snprintf(out,sz,"WARN");  return; }
    if (strstr(check," 4")&&strstr(check,"HTTP")) { snprintf(out,sz,"WARN"); return; }
    {
        const char *bp=strstr(check,"backend ");
        if (bp) {
            int avail=0,total=0;
            if (sscanf(bp+8,"%d/%d",&avail,&total)==2&&total>0&&avail<total)
                { snprintf(out,sz,"WARN"); return; }
        }
    }
    snprintf(out,sz,"OK");
}

static void compute_ai_analysis() {
    int errors=0, warns=0;
    const char* checks[] = {
        state.overall_status, state.m_m_status, state.m_s_status,
        state.m_sync, state.r_m_status, state.r_s_status, state.r_sync,
        state.lb_ping_status, state.dns_ping_status,
        state.nc1_ping_status, state.nc2_ping_status, state.nfs_ping_status,
        NULL
    };
    for (int i=0; checks[i]; i++) {
        if (strcmp(checks[i],"ERROR")==0) errors++;
        else if (strcmp(checks[i],"WARN")==0) warns++;
    }
    int total=11, healthy=total-errors-warns, pct=(healthy*100)/total;
    if (errors==0&&warns==0)
        snprintf(ai_line1,sizeof(ai_line1),"All systems nominal \u2014 cluster health 100%%");
    else if (errors==0)
        snprintf(ai_line1,sizeof(ai_line1),"Health %d%% \u2014 %d warning(s) detected, no critical faults",pct,warns);
    else
        snprintf(ai_line1,sizeof(ai_line1),"Health %d%% \u2014 %d error(s), %d warning(s) require attention",pct,errors,warns);

    int db_ok=(strcmp(state.m_sync,"OK")==0), red_ok=(strcmp(state.r_sync,"OK")==0);
    if (db_ok&&red_ok)
        snprintf(ai_line2,sizeof(ai_line2),"Replication: MariaDB GTID in sync, Redis replication stable");
    else if (!db_ok&&!red_ok)
        snprintf(ai_line2,sizeof(ai_line2),"WARNING: Both MariaDB GTID and Redis replication show issues");
    else if (!db_ok)
        snprintf(ai_line2,sizeof(ai_line2),"MariaDB GTID drift detected \u2014 check master/slave GTID delta");
    else
        snprintf(ai_line2,sizeof(ai_line2),"Redis replication degraded \u2014 inspect replication link status");

    int nc_ok=(strcmp(state.nc1_ping_status,"OK")==0&&strcmp(state.nc2_ping_status,"OK")==0);
    int nfs_ok=(strcmp(state.nfs_ping_status,"OK")==0);
    int lb_ok=(strcmp(state.lb_ping_status,"OK")==0);
    if (nc_ok&&nfs_ok&&lb_ok)
        snprintf(ai_line3,sizeof(ai_line3),"Nextcloud, NFS storage, and load balancer all reachable");
    else {
        char parts[MAX_VAL]="";
        if (!lb_ok)  strncat(parts,"LoadBalancer ", sizeof(parts)-strlen(parts)-1);
        if (!nc_ok)  strncat(parts,"Nextcloud ",   sizeof(parts)-strlen(parts)-1);
        if (!nfs_ok) strncat(parts,"NFS ",         sizeof(parts)-strlen(parts)-1);
        snprintf(ai_line3,sizeof(ai_line3),"Unreachable nodes: %s\u2014 investigate connectivity",parts);
    }
}

/* ── State loading ────────────────────────────────────────────────────────── */
void load_state() {
    time_t t=time(NULL); struct tm tm=*localtime(&t);
    char now[64]; strftime(now,sizeof(now),"%Y-%m-%d %H:%M:%S",&tm);

    if (access(config.state_file,R_OK)==0) parse_env_file(config.state_file);
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
    get_env_or("SYNCMON_MESSAGE",          "Waiting for daemon…",  state.message,        sizeof(state.message));
    get_env_or("SYNCMON_TIMESTAMP",        now,                    state.timestamp,      sizeof(state.timestamp));
    get_env_or("MYSQL_CHECK_TIMESTAMP",    state.timestamp,        state.m_chk,          sizeof(state.m_chk));
    get_env_or("REDIS_CHECK_TIMESTAMP",    state.timestamp,        state.r_chk,          sizeof(state.r_chk));
    char mh[64],mp[64],sh[64],sp[64];
    get_env_or("MYSQL_MASTER_HOST","unknown",mh,sizeof(mh)); get_env_or("MYSQL_MASTER_PORT","?",mp,sizeof(mp));
    snprintf(state.m_m_ep,sizeof(state.m_m_ep),"%s:%s",mh,mp);
    get_env_or("MYSQL_SLAVE_HOST", "unknown",sh,sizeof(sh)); get_env_or("MYSQL_SLAVE_PORT", "?",sp,sizeof(sp));
    snprintf(state.m_s_ep,sizeof(state.m_s_ep),"%s:%s",sh,sp);
    get_env_or("REDIS_MASTER_HOST","unknown",mh,sizeof(mh)); get_env_or("REDIS_MASTER_PORT","?",mp,sizeof(mp));
    snprintf(state.r_m_ep,sizeof(state.r_m_ep),"%s:%s",mh,mp);
    get_env_or("REDIS_SLAVE_HOST", "unknown",sh,sizeof(sh)); get_env_or("REDIS_SLAVE_PORT", "?",sp,sizeof(sp));
    snprintf(state.r_s_ep,sizeof(state.r_s_ep),"%s:%s",sh,sp);
#define LOAD_COMP(pfx,KEY) \
    get_env_or(KEY"_HOST",           "N/A",   state.pfx##_host,        sizeof(state.pfx##_host)); \
    get_env_or(KEY"_PING",           "N/A",   state.pfx##_ping,        sizeof(state.pfx##_ping)); \
    get_env_or(KEY"_PING_STATUS",    "N/A",   state.pfx##_ping_status, sizeof(state.pfx##_ping_status)); \
    get_env_or(KEY"_CHECK",          "N/A",   state.pfx##_check,       sizeof(state.pfx##_check)); \
    get_env_or(KEY"_CHECK_TIMESTAMP","never", state.pfx##_chk,         sizeof(state.pfx##_chk));
    LOAD_COMP(lb,  "LB")
    LOAD_COMP(dns, "DNS")
    LOAD_COMP(nc1, "NC1")
    LOAD_COMP(nc2, "NC2")
    LOAD_COMP(nfs, "NFS")
#undef LOAD_COMP

    hist_mysql[hist_idx] = status_to_val(state.m_sync);
    hist_redis[hist_idx] = status_to_val(state.r_sync);
    hist_idx = (hist_idx+1) % HIST_SIZE;
    compute_ai_analysis();
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  UI PRIMITIVES
 * ═══════════════════════════════════════════════════════════════════════════ */
int tb_print_custom(int x, int y, uint16_t fg, uint16_t bg, const char *str) {
    while (*str) { uint32_t u; str+=tb_utf8_char_to_unicode(&u,str); tb_set_cell(x++,y,u,fg,bg); }
    return 0;
}
void tb_print_fixed(int x, int y, uint16_t fg, uint16_t bg, const char *str, int w) {
    int p=0;
    while (*str&&p<w) { uint32_t u; str+=tb_utf8_char_to_unicode(&u,str); tb_set_cell(x++,y,u,fg,bg); p++; }
    while (p++<w) tb_set_cell(x++,y,' ',fg,bg);
}
void tb_print_center(int x, int y, uint16_t fg, uint16_t bg, const char *str, int w) {
    int len=0; const char* p=str;
    while (*p) { uint32_t u; p+=tb_utf8_char_to_unicode(&u,p); len++; }
    int pad=(w-len)/2; if(pad<0) pad=0;
    for(int i=0;i<pad;i++) tb_set_cell(x+i,y,' ',fg,bg);
    tb_print_fixed(x+pad,y,fg,bg,str,w-pad);
}
void tb_hline(int x, int y, uint32_t ch, uint16_t fg, uint16_t bg, int len) {
    for(int i=0;i<len;i++) tb_set_cell(x+i,y,ch,fg,bg);
}
void tb_fill(int x, int y, int w, int h, uint16_t fg, uint16_t bg) {
    for(int r=0;r<h;r++) for(int c=0;c<w;c++) tb_set_cell(x+c,y+r,' ',fg,bg);
}

uint16_t get_status_fg(const char* s, Theme* th) {
    if (strcmp(s,"OK")==0)    return th->ok;
    if (strcmp(s,"WARN")==0)  return th->warn;
    if (strcmp(s,"ERROR")==0) return th->err;
    return th->skip;
}

/* Status badge: " ✔ OK  " / " ⚠ WARN " / " ✘ ERROR" — width 9 */
void draw_badge(int x, int y, const char* s, Theme* th) {
    uint16_t fg=get_status_fg(s,th);
    uint16_t bg=th->badge_bg;
    const char *sym, *label;
    if      (strcmp(s,"OK")==0)    { sym=SYM_OK;   label="OK   "; }
    else if (strcmp(s,"WARN")==0)  { sym=SYM_WARN; label="WARN "; }
    else if (strcmp(s,"ERROR")==0) { sym=SYM_ERR;  label="ERROR"; }
    else                           { sym=SYM_SKIP; label=s;       }
    tb_set_cell(x,   y,' ',fg,bg);
    tb_print_custom(x+1,y,fg|TB_BOLD,bg,sym);
    tb_set_cell(x+2, y,' ',fg,bg);
    tb_print_fixed(x+3,y,fg|TB_BOLD,bg,label,5);
    tb_set_cell(x+8, y,' ',fg,bg);
}

/* Glow bar, width bar_w */
void draw_glow_bar(int x, int y, const char* s, Theme* th, int bar_w) {
    int fill=0;
    if      (strcmp(s,"OK")==0)    fill=bar_w;
    else if (strcmp(s,"WARN")==0)  fill=(bar_w*5)/8;
    else if (strcmp(s,"ERROR")==0) fill=(bar_w*2)/8;
    else if (strcmp(s,"N/A")==0)   fill=1;
    uint16_t fg=get_status_fg(s,th);
    for(int i=0;i<bar_w;i++) {
        uint32_t ch=(i<fill)?BAR_FULL:BAR_LOW;
        uint16_t c=(i<fill)?fg:th->skip;
        tb_set_cell(x+i,y,ch,c,th->bg);
    }
}

/* Rounded box with ◈ title marker */
void draw_box(int x, int y, int w, int h, uint16_t fg, const char* title, Theme* th) {
    tb_set_cell(x,    y,    0x256D,fg,th->bg); tb_set_cell(x+w-1,y,    0x256E,fg,th->bg);
    tb_set_cell(x,    y+h-1,0x2570,fg,th->bg); tb_set_cell(x+w-1,y+h-1,0x256F,fg,th->bg);
    for(int i=1;i<w-1;i++) { tb_set_cell(x+i,y,0x2500,fg,th->bg); tb_set_cell(x+i,y+h-1,0x2500,fg,th->bg); }
    for(int i=1;i<h-1;i++) { tb_set_cell(x,y+i,0x2502,fg,th->bg); tb_set_cell(x+w-1,y+i,0x2502,fg,th->bg); }
    if (title) {
        int tl=0; const char* tp=title;
        while(*tp){uint32_t u;tp+=tb_utf8_char_to_unicode(&u,tp);tl++;}
        int tx=x+(w-tl-4)/2; if(tx<x+1) tx=x+1;
        tb_set_cell(tx,y,' ',th->bg,th->bg);
        tb_set_cell(tx+1,y,0x25C8,th->accent,th->bg);
        tb_set_cell(tx+2,y,' ',th->bg,th->bg);
        tb_print_custom(tx+3,y,th->hdr_fg|TB_BOLD,th->bg,title);
        tb_set_cell(tx+3+tl,y,' ',th->bg,th->bg);
        tb_set_cell(tx+3+tl+1,y,0x25C8,th->accent,th->bg);
        tb_set_cell(tx+3+tl+2,y,' ',th->bg,th->bg);
    }
}

/* Double-line header box */
void draw_header_box(int x, int y, int w, int h, Theme* th) {
    tb_set_cell(x,    y,    0x2554,th->box1,th->hdr_bg);
    tb_set_cell(x+w-1,y,    0x2557,th->box1,th->hdr_bg);
    tb_set_cell(x,    y+h-1,0x255A,th->box1,th->hdr_bg);
    tb_set_cell(x+w-1,y+h-1,0x255D,th->box1,th->hdr_bg);
    for(int i=1;i<w-1;i++) { tb_set_cell(x+i,y,0x2550,th->box1,th->hdr_bg); tb_set_cell(x+i,y+h-1,0x2550,th->box1,th->hdr_bg); }
    for(int i=1;i<h-1;i++) { tb_fill(x,y+i,w,1,th->hdr_fg,th->hdr_bg); tb_set_cell(x,y+i,0x2551,th->box1,th->hdr_bg); tb_set_cell(x+w-1,y+i,0x2551,th->box1,th->hdr_bg); }
}

/* Sparkline graph */
uint32_t braille_bar(int h1, int h2) {
    if(h1<0)h1=0;if(h1>4)h1=4;if(h2<0)h2=0;if(h2>4)h2=4;
    uint32_t b=0x2800;
    if(h1>=1)b|=0x40;if(h1>=2)b|=0x04;if(h1>=3)b|=0x02;if(h1>=4)b|=0x01;
    if(h2>=1)b|=0x80;if(h2>=2)b|=0x20;if(h2>=3)b|=0x10;if(h2>=4)b|=0x08;
    return b;
}
void draw_graph(int x, int y, int* hd, int head, int mw, Theme* th) {
    for(int c=0;c<mw;c++) {
        int idx=head-1-(mw-1-c); while(idx<0)idx+=HIST_SIZE; idx%=HIST_SIZE;
        int v=hd[idx];
        if(v==-1){tb_set_cell(x+c,y,0x2508,th->skip,th->bg);continue;}
        if(v==0) {tb_set_cell(x+c,y,0x2588,th->err|TB_BOLD,th->bg);continue;}
        uint16_t col=(v==2)?th->warn:th->ok;
        if(config.use_braille) tb_set_cell(x+c,y,braille_bar(v,v),col,th->bg);
        else { uint32_t bl[]={0x2581,0x2582,0x2583,0x2584,0x2585,0x2586,0x2587,0x2588}; tb_set_cell(x+c,y,bl[v*2>7?7:v*2-1],col,th->bg); }
    }
}

/* Theme menu */
void draw_theme_menu(Theme* th) {
    if(!theme_menu_open) return;
    int w=36, h=num_themes+4, sx=(tb_width()-w)/2, sy=(tb_height()-h)/2;
    tb_fill(sx,sy,w,h,th->fg,th->bg);
    draw_box(sx,sy,w,h,th->accent,"Select Theme",th);
    tb_print_center(sx+1,sy+1,th->accent,th->bg,"↑/↓ navigate  Enter select  Esc cancel",w-2);
    tb_hline(sx+1,sy+2,0x2508,th->skip,th->bg,w-2);
    for(int i=0;i<num_themes;i++) {
        uint16_t ibg=(i==theme_menu_sel)?th->accent:th->bg;
        uint16_t ifg=(i==theme_menu_sel)?th->bg:th->fg;
        tb_fill(sx+2,sy+3+i,w-4,1,ifg,ibg);
        tb_print_fixed(sx+3,sy+3+i,ifg|(i==theme_menu_sel?TB_BOLD:0),ibg,themes[i].name,w-6);
        if(i==theme_menu_sel) tb_print_custom(sx+w-5,sy+3+i,th->bg|TB_BOLD,ibg," ◀ ");
    }
}

void format_shortened_left(char *buf, size_t bsz, const char* prefix, const char* val, int maxlen) {
    int pl=strlen(prefix), vl=strlen(val);
    if(pl+vl<=maxlen) snprintf(buf,bsz,"%s%s",prefix,val);
    else { int kl=maxlen-pl-3; if(kl<0) snprintf(buf,bsz,"%.*s",maxlen,prefix); else snprintf(buf,bsz,"%s...%s",prefix,val+(vl-kl)); }
}

const char* get_spinner(int tick) {
    switch(config.spinner_style) {
        case 1: return SPIN_DOTS[tick%10];
        case 2: return SPIN_PULSE[tick%10];
        case 3: return SPIN_ARROW[tick%8];
        default:return SPIN_BRAILLE[tick%10];
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  PANEL: simple node panel (LB, NC1/2, NFS, DNS)
 *
 *  ╭─ ◈ TITLE ◈ ─────────────────────────────────╮
 *  │ ► Host   :  172.31.x.x                       │
 *  │ ➤ Ping   :  ✔ OK     ████████░░░  3ms        │
 *  │ ◉ Check  :  ✔ OK     ████████░░░  detail      │
 *  │ ⧖ Checked:  2026-05-24 14:18:00               │
 *  ╰──────────────────────────────────────────────╯
 * ═══════════════════════════════════════════════════════════════════════════ */
void draw_node_panel(int x, int y, int w, int h,
                     const char* title,
                     const char* host,
                     const char* ping_status,
                     const char* ping,
                     const char* check,
                     const char* chk_ts,
                     Theme* th)
{
    draw_box(x,y,w,h,th->box2,title,th);
    char buf[MAX_VAL];
    int lx=x+2;
    /* row 1: host */
    tb_print_custom(lx,y+1,th->accent,th->bg,SYM_HOST);
    snprintf(buf,sizeof(buf)," Host   : %s",host);
    tb_print_fixed(lx+1,y+1,th->fg,th->bg,buf,w-4);
    /* row 2: ping */
    tb_print_custom(lx,y+2,th->accent,th->bg,SYM_ARROW_R);
    tb_print_custom(lx+2,y+2,th->fg,th->bg," Ping   :");
    draw_badge(lx+12,y+2,ping_status,th);
    draw_glow_bar(lx+22,y+2,ping_status,th,10);
    tb_print_fixed(lx+33,y+2,get_status_fg(ping_status,th),th->bg,ping,w-lx-34+x);
    /* row 3: check */
    char chk_st[16]; derive_check_status(check,chk_st,sizeof(chk_st));
    tb_print_custom(lx,y+3,th->accent,th->bg,SYM_PULSE);
    tb_print_custom(lx+2,y+3,th->fg,th->bg," Check  :");
    draw_badge(lx+12,y+3,chk_st,th);
    draw_glow_bar(lx+22,y+3,chk_st,th,10);
    tb_print_fixed(lx+33,y+3,get_status_fg(chk_st,th),th->bg,check,w-lx-34+x);
    /* row 4: timestamp */
    tb_print_custom(lx,y+4,th->accent,th->bg,SYM_CLOCK);
    snprintf(buf,sizeof(buf)," Checked: %s",chk_ts);
    tb_print_fixed(lx+1,y+4,th->skip,th->bg,buf,w-4);
    (void)h;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  PANEL: MariaDB replication (split, with animated center sub-box)
 *
 *  ╭─ ◈ ▼ MariaDB ◈ ──────────────────────────────────────────────────────╮
 *  │ ► Host: 172.31.49.233:3306   │   ► Host: 172.31.40.234:3306          │
 *  │ ★ Master  ✔ OK  ████████░░   │   ☆ Slave   ✔ OK  ████████░░         │
 *  │                              │                                        │
 *  │        ══════════════════> ┌──────────────────┐                      │
 *  │                            │ M-GTID: uuid:576 │  (sync anim)         │
 *  │                            │ S-GTID: uuid:576 │                      │
 *  │                            └──────────────────┘                      │
 *  ╰──────────────────────────────────────────────────────────────────────╯
 * ═══════════════════════════════════════════════════════════════════════════ */
void draw_mariadb_panel(int x, int y, int w, int h, int anim_tick, Theme* th) {
    draw_box(x,y,w,h,th->box2,SYM_DB " MariaDB",th);

    int half=w/2;
    /* vertical divider */
    for(int i=1;i<h-1;i++) tb_set_cell(x+half,y+i,0x2502,th->box2,th->bg);
    tb_set_cell(x+half,y,0x252C,th->box2,th->bg);
    tb_set_cell(x+half,y+h-1,0x2534,th->box2,th->bg);

    int lx=x+2, rx=x+half+2;

    /* ── Left: master ── */
    /* row 1: host */
    tb_print_custom(lx,y+1,th->accent,th->bg,SYM_HOST);
    char buf[MAX_VAL];
    snprintf(buf,sizeof(buf)," %s",state.m_m_ep);
    tb_print_fixed(lx+1,y+1,th->fg,th->bg,buf,half-4);
    /* row 2: status badge + bar */
    tb_print_custom(lx,y+2,th->accent,th->bg,SYM_MASTER);
    tb_print_custom(lx+1,y+2,th->fg,th->bg," Master :");
    draw_badge(lx+10,y+2,state.m_m_status,th);
    draw_glow_bar(lx+20,y+2,state.m_m_status,th,half-22);

    /* ── Right: slave ── */
    tb_print_custom(rx,y+1,th->accent,th->bg,SYM_HOST);
    snprintf(buf,sizeof(buf)," %s",state.m_s_ep);
    tb_print_fixed(rx+1,y+1,th->fg,th->bg,buf,w-half-4);
    tb_print_custom(rx,y+2,th->accent,th->bg,SYM_SLAVE);
    tb_print_custom(rx+1,y+2,th->fg,th->bg," Slave  :");
    draw_badge(rx+10,y+2,state.m_s_status,th);
    draw_glow_bar(rx+20,y+2,state.m_s_status,th,w-half-22);

    /* ── Sync status row ── */
    tb_print_custom(lx,y+3,th->accent,th->bg,SYM_LINK);
    tb_print_custom(lx+1,y+3,th->fg,th->bg," Sync   :");
    draw_badge(lx+10,y+3,state.m_sync,th);
    draw_glow_bar(lx+20,y+3,state.m_sync,th,w-22);

    /* ── Animated arrow (row 4) ── */
    uint16_t arrow_col = get_status_fg(state.m_sync,th);
    int arow=y+4;
    int arrow_frame = anim_tick%8;
    /* left side: animated frames */
    int arrow_x = lx;
    tb_print_custom(arrow_x, arow, arrow_col|TB_BOLD, th->bg, ANIM_R[arrow_frame]);

    /* ── GTID sub-box in center (rows 4–6) ── */
    int sbw=half-2, sbx=x+(w-sbw)/2;
    /* only draw box outline on row 5-6 */
    int sb_y=y+5;
    draw_box(sbx,sb_y,sbw,3,th->box1,NULL,th);

    int gtid_max=sbw-4;
    format_shortened_left(buf,sizeof(buf),"M-GTID: ",state.m_m_gtid,gtid_max);
    tb_print_fixed(sbx+2,sb_y+1,th->skip,th->bg,buf,sbw-4);
    format_shortened_left(buf,sizeof(buf),"S-GTID: ",state.m_s_gtid,gtid_max);
    tb_print_fixed(sbx+2,sb_y+2,th->skip,th->bg,buf,sbw-4);

    /* checked timestamp bottom */
    snprintf(buf,sizeof(buf),SYM_CLOCK " Checked: %s",state.m_chk);
    tb_print_fixed(lx+1,y+h-2,th->skip,th->bg,buf,w-4);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  PANEL: Redis replication (split, animated arrow left, detail sub-box)
 * ═══════════════════════════════════════════════════════════════════════════ */
void draw_redis_panel(int x, int y, int w, int h, int anim_tick, Theme* th) {
    draw_box(x,y,w,h,th->box2,SYM_REDIS " Redis",th);

    int half=w/2;
    for(int i=1;i<h-1;i++) tb_set_cell(x+half,y+i,0x2502,th->box2,th->bg);
    tb_set_cell(x+half,y,0x252C,th->box2,th->bg);
    tb_set_cell(x+half,y+h-1,0x2534,th->box2,th->bg);

    int lx=x+2, rx=x+half+2;
    char buf[MAX_VAL];

    /* ── Left: master ── */
    tb_print_custom(lx,y+1,th->accent,th->bg,SYM_HOST);
    snprintf(buf,sizeof(buf)," %s",state.r_m_ep);
    tb_print_fixed(lx+1,y+1,th->fg,th->bg,buf,half-4);
    tb_print_custom(lx,y+2,th->accent,th->bg,SYM_MASTER);
    tb_print_custom(lx+1,y+2,th->fg,th->bg," Master :");
    draw_badge(lx+10,y+2,state.r_m_status,th);
    draw_glow_bar(lx+20,y+2,state.r_m_status,th,half-22);

    /* ── Right: slave ── */
    tb_print_custom(rx,y+1,th->accent,th->bg,SYM_HOST);
    snprintf(buf,sizeof(buf)," %s",state.r_s_ep);
    tb_print_fixed(rx+1,y+1,th->fg,th->bg,buf,w-half-4);
    tb_print_custom(rx,y+2,th->accent,th->bg,SYM_SLAVE);
    tb_print_custom(rx+1,y+2,th->fg,th->bg," Slave  :");
    draw_badge(rx+10,y+2,state.r_s_status,th);
    draw_glow_bar(rx+20,y+2,state.r_s_status,th,w-half-22);

    /* ── Replication status ── */
    tb_print_custom(lx,y+3,th->accent,th->bg,SYM_LINK);
    tb_print_custom(lx+1,y+3,th->fg,th->bg," Repl   :");
    draw_badge(lx+10,y+3,state.r_sync,th);
    draw_glow_bar(lx+20,y+3,state.r_sync,th,w-22);

    /* ── Animated arrow LEFT (master→slave direction reversed for Redis) ── */
    uint16_t arrow_col=get_status_fg(state.r_sync,th);
    int arow=y+4;
    int arrow_frame=anim_tick%8;
    tb_print_custom(x+half-10,arow,arrow_col|TB_BOLD,th->bg,ANIM_L[arrow_frame]);

    /* ── Detail sub-box center (rows 5-6) ── */
    int sbw=half-2, sbx=x+(w-sbw)/2, sb_y=y+5;
    draw_box(sbx,sb_y,sbw,3,th->box1,NULL,th);
    snprintf(buf,sizeof(buf),"Detail: %s",state.r_det);
    tb_print_fixed(sbx+2,sb_y+1,th->skip,th->bg,buf,sbw-4);
    snprintf(buf,sizeof(buf),"Checked: %s",state.r_chk);
    tb_print_fixed(sbx+2,sb_y+2,th->skip,th->bg,buf,sbw-4);

    snprintf(buf,sizeof(buf),SYM_CLOCK " Checked: %s",state.r_chk);
    tb_print_fixed(lx+1,y+h-2,th->skip,th->bg,buf,w-4);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  MAIN DRAW
 * ═══════════════════════════════════════════════════════════════════════════ */
void draw_ui(int anim_tick) {
    Theme *th=&themes[current_theme];
    tb_set_clear_attrs(th->fg,th->bg);
    tb_clear();

    int sw=tb_width();
    int bw=config.dash_w+2;
    int bx=(sw-bw)/2; if(bx<0) bx=0;
    char buf[1024];
    int y=0;

    /* ── HEADER ─────────────────────────────────────────────────────────── */
    draw_header_box(bx,y,bw,3,th);
    snprintf(buf,sizeof(buf),"%s SYNCMON %s  Nextcloud HA Cluster Monitor",SYM_AI,SYM_AI);
    tb_print_center(bx+1,y+1,th->hdr_fg|TB_BOLD,th->hdr_bg,buf,bw-2);
    /* spinner + timestamp right-aligned inside header */
    const char* spin=get_spinner(anim_tick);
    snprintf(buf,sizeof(buf),"%s  Refresh %ds  Updated: %s  %s",
             spin,config.display_refresh,state.timestamp,spin);
    tb_print_center(bx+1,y+2,th->accent,th->hdr_bg,buf,bw-2);

    /* ── OVERVIEW + AI ──────────────────────────────────────────────────── */
    y=3;
    int ov_h = config.show_ai ? 7 : 4;
    draw_box(bx,y,bw,ov_h,th->box1,"Overview",th);

    /* overview row 1: overall + summary bars */
    tb_print_custom(bx+2,y+1,th->fg,th->bg,"Overall Status :");
    draw_badge(bx+18,y+1,state.overall_status,th);
    /* MariaDB compact bars */
    tb_print_custom(bx+28,y+1,th->highlight,th->bg,SYM_DB " M:");
    draw_glow_bar(bx+32,y+1,state.m_m_status,th,6);
    draw_glow_bar(bx+39,y+1,state.m_s_status,th,6);
    draw_glow_bar(bx+46,y+1,state.m_sync,th,6);
    /* Redis compact bars */
    tb_print_custom(bx+53,y+1,th->highlight,th->bg,SYM_REDIS " R:");
    draw_glow_bar(bx+57,y+1,state.r_m_status,th,6);
    draw_glow_bar(bx+64,y+1,state.r_s_status,th,6);
    draw_glow_bar(bx+71,y+1,state.r_sync,th,6);

    /* overview row 2: endpoint summary */
    snprintf(buf,sizeof(buf),SYM_DB "  %s " SYM_LINK " %s",state.m_m_ep,state.m_s_ep);
    tb_print_fixed(bx+2,y+2,th->fg,th->bg,buf,bw/2-2);
    snprintf(buf,sizeof(buf),SYM_REDIS "  %s " SYM_LINK " %s",state.r_m_ep,state.r_s_ep);
    tb_print_fixed(bx+bw/2,y+2,th->fg,th->bg,buf,bw/2-2);

    /* AI panel rows */
    if (config.show_ai) {
        tb_hline(bx+1,y+3,0x2508,th->skip,th->bg,bw-2);
        tb_print_custom(bx+2,y+3,th->accent,th->bg,SYM_AI " AI Analysis");
        tb_print_custom(bx+2,y+4,SPIN_PULSE[anim_tick%10][0]?th->accent:th->accent,th->bg,SPIN_PULSE[anim_tick%10]);
        tb_print_fixed(bx+4,y+4,th->hdr_fg,th->bg,ai_line1,bw-6);
        tb_print_custom(bx+2,y+5,th->ok,th->bg,SYM_DB);
        tb_print_fixed(bx+4,y+5,th->fg,th->bg,ai_line2,bw-6);
        tb_print_custom(bx+2,y+6,th->accent,th->bg,SYM_NC);
        tb_print_fixed(bx+4,y+6,th->fg,th->bg,ai_line3,bw-6);
    }
    y+=ov_h+1;

    /* ── LOADBALANCER (full width, h=6) ─────────────────────────────────── */
    draw_node_panel(bx,y,bw,6,SYM_LB " Loadbalancer",
                   state.lb_host,state.lb_ping_status,state.lb_ping,
                   state.lb_check,state.lb_chk,th);
    y+=7;

    /* ── NEXTCLOUD 1 | NEXTCLOUD 2 (50/50, h=6) ─────────────────────────── */
    int nc_w=bw/2, nc2_w=bw-nc_w;
    draw_node_panel(bx,      y,nc_w, 6,SYM_NC " Nextcloud 1",
                   state.nc1_host,state.nc1_ping_status,state.nc1_ping,
                   state.nc1_check,state.nc1_chk,th);
    draw_node_panel(bx+nc_w, y,nc2_w,6,SYM_NC " Nextcloud 2",
                   state.nc2_host,state.nc2_ping_status,state.nc2_ping,
                   state.nc2_check,state.nc2_chk,th);
    y+=7;

    /* ── MARIADB REPLICATION PANEL (full width, h=9) ─────────────────────── */
    draw_mariadb_panel(bx,y,bw,9,anim_tick,th);
    y+=10;

    /* ── REDIS REPLICATION PANEL (full width, h=9) ───────────────────────── */
    draw_redis_panel(bx,y,bw,9,anim_tick,th);
    y+=10;

    /* ── NFS | DNS (50/50, h=6) ─────────────────────────────────────────── */
    int nfs_w=bw/2, dns_w=bw-nfs_w;
    draw_node_panel(bx,        y,nfs_w,6,SYM_NFS " NFS",
                   state.nfs_host,state.nfs_ping_status,state.nfs_ping,
                   state.nfs_check,state.nfs_chk,th);
    draw_node_panel(bx+nfs_w,  y,dns_w,6,SYM_DNS " DNS",
                   state.dns_host,state.dns_ping_status,state.dns_ping,
                   state.dns_check,state.dns_chk,th);
    y+=7;

    /* ── HISTORY / DETAILS ──────────────────────────────────────────────── */
    draw_box(bx,y,bw,5,th->box1,SYM_HISTORY " History",th);
    tb_print_custom(bx+2,y+1,th->highlight,th->bg,SYM_DB);
    tb_print_custom(bx+4,y+1,th->fg,th->bg," MariaDB sync :");
    draw_graph(bx+19,y+1,hist_mysql,hist_idx,bw-21,th);
    tb_print_custom(bx+2,y+2,th->highlight,th->bg,SYM_REDIS);
    tb_print_custom(bx+4,y+2,th->fg,th->bg," Redis  repl  :");
    draw_graph(bx+19,y+2,hist_redis,hist_idx,bw-21,th);
    snprintf(buf,sizeof(buf),"%s  %s",SYM_BULLET,state.message);
    tb_print_fixed(bx+2,y+3,th->accent,th->bg,buf,bw-4);
    y+=6;

    /* ── FOOTER keybinding legend ───────────────────────────────────────── */
    tb_hline(bx,y,0x2508,th->skip,th->bg,bw);
    const char* keys[]={"q Quit","t Themes","g Graph","s Spinner","a AI Panel",NULL};
    int kx=bx+1;
    for(int i=0;keys[i];i++){
        tb_set_cell(kx,y+1,'[',th->skip,th->bg);
        tb_print_custom(kx+1,y+1,th->accent|TB_BOLD,th->bg,keys[i]);
        tb_set_cell(kx+1+(int)strlen(keys[i]),y+1,']',th->skip,th->bg);
        kx+=strlen(keys[i])+4;
    }

    draw_theme_menu(th);
    tb_present();
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  USAGE + MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */
static void print_usage(const char* prog) {
    printf("Usage: %s [OPTIONS]\n\n"
           "Options:\n"
           "  -r, --refresh <seconds>   Refresh interval in seconds (default: 2)\n"
           "  -f, --file    <path>      Path to state file (default: %s)\n"
           "  --no-braille              Use classic block graphs\n"
           "  -h, --help                Show this message\n\n"
           "Keyboard:\n"
           "  q / Ctrl+C   Quit\n"
           "  t            Theme menu (arrows + Enter)\n"
           "  g            Toggle graph style\n"
           "  s            Cycle spinner style\n"
           "  a            Toggle AI analysis panel\n",
           prog, STATE_FILE_DEFAULT);
}

int main(int argc, char** argv) {
    for(int i=0;i<HIST_SIZE;i++){hist_mysql[i]=-1;hist_redis[i]=-1;}
    for(int i=1;i<argc;i++){
        if      (strcmp(argv[i],"--no-braille")==0) config.use_braille=0;
        else if ((strcmp(argv[i],"-r")==0||strcmp(argv[i],"--refresh")==0)&&i+1<argc){
            config.display_refresh=atoi(argv[++i]);
            if(config.display_refresh<=0){fprintf(stderr,"ERROR: refresh must be >0\n");return 1;}
        } else if ((strcmp(argv[i],"-f")==0||strcmp(argv[i],"--file")==0)&&i+1<argc){
            strncpy(config.state_file,argv[++i],sizeof(config.state_file)-1);
        } else if (strcmp(argv[i],"--help")==0||strcmp(argv[i],"-h")==0){
            print_usage(argv[0]); return 0;
        } else { fprintf(stderr,"ERROR: unknown argument: %s\n",argv[i]); return 1; }
    }
    if(config.display_refresh<=0) config.display_refresh=2;
    srand(time(NULL));
    tb_init();
    tb_set_output_mode(TB_OUTPUT_256);
    tb_set_input_mode(TB_INPUT_ESC);
    signal(SIGINT,handle_sig); signal(SIGTERM,handle_sig);

    struct tb_event ev;
    uint64_t last_update=0; int anim_tick=0;
    load_state(); last_update=get_time_ms();

    while(keep_running){
        uint64_t now=get_time_ms();
        if(now-last_update>=(uint64_t)config.display_refresh*1000){load_state();last_update=now;}
        anim_tick=(now/100)%10;
        draw_ui(anim_tick);
        int res=tb_peek_event(&ev,100);
        if(res==TB_OK){
            if(theme_menu_open){
                if(ev.type==TB_EVENT_KEY){
                    if     (ev.key==TB_KEY_ARROW_UP)   theme_menu_sel=(theme_menu_sel-1+num_themes)%num_themes;
                    else if(ev.key==TB_KEY_ARROW_DOWN) theme_menu_sel=(theme_menu_sel+1)%num_themes;
                    else if(ev.key==TB_KEY_ENTER)      {current_theme=theme_menu_sel;theme_menu_open=0;}
                    else if(ev.key==TB_KEY_ESC||ev.ch=='q') theme_menu_open=0;
                }
            } else {
                if(ev.type==TB_EVENT_KEY){
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

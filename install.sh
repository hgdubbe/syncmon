#!/usr/bin/env bash
# SyncMon Installer — new-gui branch
# Usage: sudo bash install.sh
set -euo pipefail

RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'; CYN='\033[0;36m'; BLD='\033[1m'; RST='\033[0m'

die()  { echo -e "${RED}[ERROR]${RST} $*" >&2; exit 1; }
info() { echo -e "${CYN}[INFO]${RST}  $*"; }
ok()   { echo -e "${GRN}[ OK ]${RST}  $*"; }
warn() { echo -e "${YLW}[WARN]${RST}  $*"; }
ask()  { echo -e "${BLD}$*${RST}"; }

# ── Root check ─────────────────────────────────────────────────────────────────
[[ $EUID -eq 0 ]] || die "Please run as root: sudo bash install.sh"

echo -e "\n${YLW}${BLD}╔══════════════════════════════════════════════╗"
echo    "║     SyncMon Installer  [new-gui branch]     ║"
echo -e "╚══════════════════════════════════════════════╝${RST}"
warn "This installs the new-gui branch (animated redesigned TUI)."
warn "For the stable release use the main branch installer."
echo

# ── 1. Clone repository ────────────────────────────────────────────────────────
REPO_URL="https://github.com/hgdubbe/syncmon"
BRANCH="new-gui"
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

info "Cloning branch '${BRANCH}' from ${REPO_URL} ..."
git clone --depth=1 --branch "$BRANCH" "$REPO_URL" "$TMP_DIR/syncmon" 2>&1 | sed 's/^/  /'
ok "Repository downloaded (branch: ${BRANCH})."
cd "$TMP_DIR/syncmon"

# ── 2. Dependency check ────────────────────────────────────────────────────────
info "Checking runtime dependencies ..."
missing=()
command -v mysql     >/dev/null 2>&1 || missing+=("mysql")
command -v redis-cli >/dev/null 2>&1 || missing+=("redis-cli")
if [[ ${#missing[@]} -gt 0 ]]; then
    warn "The following binaries were NOT found in PATH:"
    for m in "${missing[@]}"; do echo "    • $m"; done
    warn "The daemon requires these at runtime. Install them before starting the service."
else
    ok "mysql and redis-cli are available."
fi

# ── 3. (Optional) Recompile daemon ────────────────────────────────────────────
DAEMON_BIN="./syncmon-daemon"
ask "\nRecompile the daemon from source? [y/N] "
read -r ans
if [[ "$ans" =~ ^[Yy]$ ]]; then
    command -v gcc >/dev/null 2>&1 || die "gcc not found. Install build-essential first."
    info "Compiling syncmon-daemon ..."
    gcc -O3 -Wall -static syncmon-daemon.c -o syncmon-daemon-new
    DAEMON_BIN="./syncmon-daemon-new"
    ok "Daemon compiled."
else
    info "Using pre-built daemon binary from repository."
fi

# ── 4. Install daemon binary ───────────────────────────────────────────────────
info "Installing daemon to /usr/bin/syncmon-daemon ..."
install -m 0755 "$DAEMON_BIN" /usr/bin/syncmon-daemon
ok "Daemon installed."

# ── 5. Install configuration file ─────────────────────────────────────────────
mkdir -p /etc/syncmon.d
if [[ -f /etc/syncmon.d/config.conf ]]; then
    warn "Config already exists at /etc/syncmon.d/config.conf — keeping existing file."
    install -m 0640 ressources/syncmon.conf /etc/syncmon.d/config.conf.new
    info "New template saved as /etc/syncmon.d/config.conf.new for reference."
else
    install -m 0640 ressources/syncmon.conf /etc/syncmon.d/config.conf
    ok "Config installed. Edit /etc/syncmon.d/config.conf before starting the daemon."
fi

# ── 6. Create log directory ────────────────────────────────────────────────────
mkdir -p /var/log/syncmon
ok "Log directory /var/log/syncmon ready."

# ── 7. Register systemd service ───────────────────────────────────────────────
info "Installing systemd service unit ..."
install -m 0644 syncmon-daemon.service /etc/systemd/system/syncmon-daemon.service
systemctl daemon-reload
ok "Service registered: syncmon-daemon"

ask "\nEnable syncmon-daemon to start on boot? [y/N] "
read -r ans
[[ "$ans" =~ ^[Yy]$ ]] && { systemctl enable syncmon-daemon; ok "syncmon-daemon enabled on boot."; }

ask "Start syncmon-daemon now? [y/N] "
read -r ans
if [[ "$ans" =~ ^[Yy]$ ]]; then
    systemctl start syncmon-daemon
    sleep 1
    if systemctl is-active --quiet syncmon-daemon; then
        ok "syncmon-daemon is running."
    else
        warn "Service may have failed. Check: journalctl -u syncmon-daemon -n 30"
    fi
fi

# ── 8. (Optional) Recompile TUI ───────────────────────────────────────────────
TUI_BIN="./syncmon"
ask "\nRecompile the TUI dashboard from source? [y/N] "
read -r ans
if [[ "$ans" =~ ^[Yy]$ ]]; then
    command -v gcc >/dev/null 2>&1 || die "gcc not found. Install build-essential first."
    [[ -f termbox2.h ]] || {
        info "Downloading termbox2.h ..."
        curl -fsSL "https://raw.githubusercontent.com/termbox/termbox2/master/termbox2.h" -o termbox2.h
    }
    info "Compiling syncmon TUI ..."
    gcc -O2 -march=x86-64 syncmon.c -o syncmon-new
    TUI_BIN="./syncmon-new"
    ok "TUI compiled."
else
    info "Using pre-built TUI binary from repository."
fi

# ── 9. Install TUI binary ──────────────────────────────────────────────────────
info "Installing TUI to /usr/bin/syncmon ..."
install -m 0755 "$TUI_BIN" /usr/bin/syncmon
ok "TUI installed."

# ── 10. Done ──────────────────────────────────────────────────────────────────
echo
echo -e "${GRN}${BLD}╔══════════════════════════════════════════════╗"
echo    "║   SyncMon installation complete! (new-gui)  ║"
echo -e "╚══════════════════════════════════════════════╝${RST}"
echo
echo -e "${BLD}Service management:${RST}"
echo    "  systemctl start   syncmon-daemon"
echo    "  systemctl stop    syncmon-daemon"
echo    "  systemctl status  syncmon-daemon"
echo    "  systemctl enable  syncmon-daemon"
echo
echo -e "${BLD}TUI dashboard:${RST}"
echo    "  syncmon                        # launch with defaults"
echo    "  syncmon -r 5                   # refresh every 5 seconds"
echo    "  syncmon -f /custom/state.env   # custom state file"
echo    "  syncmon --no-braille           # classic block graphs"
echo
echo -e "${BLD}TUI keyboard shortcuts:${RST}"
echo    "  q / Ctrl+C   Quit"
echo    "  t            Theme menu (↑/↓ + Enter)"
echo    "  g            Toggle graph style"
echo    "  s            Cycle spinner style (4 styles)"
echo    "  a            Toggle AI analysis panel"
echo
echo -e "${BLD}Config & logs:${RST}"
echo    "  /etc/syncmon.d/config.conf          # daemon config"
echo    "  /var/log/syncmon/syncmon.log        # daemon log"
echo    "  /var/log/syncmon/syncmon_state.env  # live state (read by TUI)"
echo

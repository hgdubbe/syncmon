# SyncMon — Nextcloud HA Cluster Monitor

> **You are on the `new-gui` branch.**
> This branch contains the redesigned TUI dashboard. For the stable release, see [`main`](https://github.com/hgdubbe/syncmon/tree/main).

**SyncMon** is a lightweight, high-performance monitoring suite for a Nextcloud high-availability stack. It monitors MariaDB/MySQL and Redis replication clusters and provides a full-stack TUI dashboard covering all infrastructure components.

It consists of two components that work together:

- **`syncmon-daemon`** — A background C service that continuously polls your database and cache nodes, writing real-time replication state to a shared environment file.
- **`syncmon`** — A terminal dashboard (TUI) written in C using `termbox2` that reads the state file and renders a live, animated grid layout covering the entire stack.

---

## Dashboard Layout

The TUI maps directly to the physical infrastructure. Each panel represents one virtual host:
╔══════════════════════════════════════════════════════════════╗
║ ✶ SYNCMON ✶ Nextcloud HA Cluster Monitor ⠋ 12:00:00 ║
╚══════════════════════════════════════════════════════════════╝
╭──────────────────────────────────────────────────────────────╮
│ Overview │ Overall: ✔ OK │ ▼M: ████ ██ ██ ▶R: ████ │
│ ✶ AI Analysis: All systems nominal — cluster health 100% │
╰──────────────────────────────────────────────────────────────╯
╭──────────────────────────────────────────────────────────────╮
│ ≈ Loadbalancer │
╰──────────────────────────────────────────────────────────────╯
╭──────────────────────────────┬───────────────────────────────╮
│ ☁ Nextcloud 1 │ ☁ Nextcloud 2 │
╰──────────────────────────────┴───────────────────────────────╯
╭──────────────────────────────────────────────────────────────╮
│ ▼ MariaDB Master │ MariaDB Slave │
│ ★ Master: ✔ OK ████████░░ │ ☆ Slave: ✔ OK ████████░░ │
│ ··══════════════> ┌─────────────────┐ │
│ Sync: ✔ OK ███████ │ M-GTID: uuid:.. │ (animated arrow) │
│ │ S-GTID: uuid:.. │ │
│ └─────────────────┘ │
╰──────────────────────────────────────────────────────────────╯
╭──────────────────────────────────────────────────────────────╮
│ ▶ Redis Master │ Redis Slave │
│ ★ Master: ✔ OK ████████░░ │ ☆ Slave: ✔ OK ████████░░ │
│ ⮜══════════════ ┌─────────────────┐ │
│ Repl: ✔ OK ████ │ Detail: link=up │ (animated arrow ←) │
│ │ Checked: ... │ │
│ └─────────────────┘ │
╰──────────────────────────────────────────────────────────────╯
╭──────────────────────────────┬───────────────────────────────╮
│ ■ NFS │ ⌘ DNS │
╰──────────────────────────────┴───────────────────────────────╯
╭──────────────────────────────────────────────────────────────╮
│ ⧗ History MariaDB: ⣾⣿⡇▁⣿ Redis: ⣾⣿⣿⡇⣿ ● message... │
╰──────────────────────────────────────────────────────────────╯
[ q Quit] [ t Themes] [ g Graph] [ s Spinner] [ a AI Panel]

text

| Panel | Virtual Host | Data Source | Status |
|---|---|---|---|
| Overview | — | State file + AI analysis | ✅ Live |
| Loadbalancer | HAProxy node | State file | ✅ Live |
| Nextcloud 1 | NC app node 1 | State file | ✅ Live |
| Nextcloud 2 | NC app node 2 | State file | ✅ Live |
| MariaDB | DB replication pair | State file | ✅ Live |
| Redis | Cache replication pair | State file | ✅ Live |
| NFS | Shared storage node | State file | ✅ Live |
| DNS | Internal DNS node | State file | ✅ Live |
| History | — | Ring buffer (600 samples) | ✅ Live |

---

## Features

### Dashboard (`syncmon`)
- **Infrastructure-mapped layout**: Each box = one virtual host, matching real network topology.
- **Animated replication arrows**: MariaDB and Redis panels show an animated sync arrow between master and slave with GTID/detail sub-box.
- **Rich status badges**: `✔ OK`, `⚠ WARN`, `✘ ERROR` with color-coded glow progress bars.
- **AI Analysis panel**: Auto-computed health summary with 3 contextual lines — overall health score, replication state, and connectivity. Toggle with `a`.
- **Historical sparklines**: Braille or block character graphs of the last 600 sync states.
- **11 built-in themes**: Default, Monokai, Dracula, Nord, Gruvbox, Cyberpunk, Calm, White Paper, Grayscale, Ocean, Lava.
- **4 spinner styles**: Braille, dots, pulse, arrow — cycle with `s`.

### Daemon (`syncmon-daemon`)
- Zero external dependencies — statically compiled.
- Uses existing `mysql` and `redis-cli` binaries for health queries.
- Config at `/etc/syncmon.d/config.conf`; state file at `/var/log/syncmon/syncmon_state.env`.
- Built-in log rotation.

---

## Quick Install

```bash
sudo bash -c "$(curl -fsSL https://raw.githubusercontent.com/hgdubbe/syncmon/new-gui/install.sh)"
```

The installer will:
1. Clone the `new-gui` branch.
2. Check for `mysql` and `redis-cli` in `$PATH`.
3. Optionally recompile the daemon and/or TUI from source.
4. Install binaries to `/usr/bin/`.
5. Install config template to `/etc/syncmon.d/config.conf`.
6. Register and optionally start/enable the `systemd` service.

For the stable installer:
```bash
sudo bash -c "$(curl -fsSL https://raw.githubusercontent.com/hgdubbe/syncmon/main/install.sh)"
```

---

## Manual Installation

### Prerequisites

- GCC (`build-essential`)
- `mysql` / `mariadb-client` and `redis-tools` in `$PATH`
- [`termbox2`](https://github.com/termbox/termbox2) (bundled as `termbox2.h`)

### Build

```bash
git clone --branch new-gui https://github.com/hgdubbe/syncmon.git
cd syncmon

# Daemon (static)
gcc -O3 -Wall -static syncmon-daemon.c -o syncmon-daemon

# TUI
gcc -O2 -march=x86-64 syncmon.c -o syncmon
```

### Install

```bash
sudo install -m 0755 syncmon-daemon /usr/bin/syncmon-daemon
sudo install -m 0755 syncmon        /usr/bin/syncmon

sudo mkdir -p /etc/syncmon.d /var/log/syncmon
sudo install -m 0640 ressources/syncmon.conf /etc/syncmon.d/config.conf

sudo nano /etc/syncmon.d/config.conf  # edit hosts/ports/credentials

sudo install -m 0644 syncmon-daemon.service /etc/systemd/system/syncmon-daemon.service
sudo systemctl daemon-reload
sudo systemctl enable --now syncmon-daemon
```

---

## Configuration

The daemon reads `/etc/syncmon.d/config.conf`. The TUI needs no config file — all options are CLI arguments.

### Daemon config keys

| Key | Default | Description |
|---|---|---|
| `CHECK_INTERVAL` | `30` | Poll interval in seconds |
| `ENABLE_MYSQL_CHECK` | `1` | Enable MariaDB checks |
| `ENABLE_REDIS_CHECK` | `1` | Enable Redis checks |
| `STARTUP_CHECK` | `1` | Run check immediately on start |
| `EXIT_ON_STARTUP_FAILURE` | `0` | Exit if startup check fails |
| `LOG_FILE` | `/var/log/syncmon/syncmon.log` | Daemon log path |
| `LOG_MAX_SIZE` | `100` | Max log size in MB before rotation |
| `STATE_FILE` | `/var/log/syncmon/syncmon_state.env` | State file path |
| `MYSQL_MASTER_HOST` | `127.0.0.1` | MariaDB master host |
| `MYSQL_MASTER_PORT` | `3306` | MariaDB master port |
| `MYSQL_SLAVE_HOST` | `127.0.0.2` | MariaDB slave host |
| `MYSQL_SLAVE_PORT` | `3306` | MariaDB slave port |
| `MYSQL_USER` | | MySQL user |
| `MYSQL_PASSWORD` | | MySQL password |
| `REDIS_MASTER_HOST` | `127.0.0.1` | Redis master host |
| `REDIS_MASTER_PORT` | `6379` | Redis master port |
| `REDIS_SLAVE_HOST` | `127.0.0.2` | Redis slave host |
| `REDIS_SLAVE_PORT` | `6379` | Redis slave port |
| `REDIS_PASSWORD` | | Redis password |

---

## Usage

### Service management

```bash
systemctl start   syncmon-daemon
systemctl stop    syncmon-daemon
systemctl status  syncmon-daemon
systemctl enable  syncmon-daemon
systemctl disable syncmon-daemon
```

### TUI dashboard

```bash
syncmon                        # launch with defaults
syncmon -r 5                   # refresh every 5 seconds
syncmon -f /custom/state.env   # use a custom state file
syncmon --no-braille           # use classic block graphs
```

### CLI reference

| Argument | Description |
|---|---|
| `-r`, `--refresh <n>` | Refresh interval in seconds (default: `2`) |
| `-f`, `--file <path>` | Path to state file |
| `--no-braille` | Start with classic block graphs |
| `-h`, `--help` | Show help |

### Keyboard controls

| Key | Action |
|---|---|
| `q` / `Ctrl+C` | Quit |
| `t` | Theme menu (↑/↓ + Enter) |
| `g` | Toggle graph style (Braille / blocks) |
| `s` | Cycle spinner style (4 styles) |
| `a` | Toggle AI analysis panel |

---

## File Locations

| Path | Description |
|---|---|
| `/usr/bin/syncmon-daemon` | Daemon binary |
| `/usr/bin/syncmon` | TUI binary |
| `/etc/syncmon.d/config.conf` | Daemon configuration |
| `/etc/systemd/system/syncmon-daemon.service` | systemd unit |
| `/var/log/syncmon/syncmon.log` | Daemon log (auto-rotated) |
| `/var/log/syncmon/syncmon_state.env` | Live state file |

---

## State File Reference

Written by the daemon to `/var/log/syncmon/syncmon_state.env`:

```ini
OVERALL_STATUS="OK"
SYNCMON_TIMESTAMP="2026-05-25 12:00:00"
SYNCMON_MESSAGE="All clusters in sync"

MYSQL_MASTER_HOST="172.31.49.233"
MYSQL_MASTER_PORT="3306"
MYSQL_SLAVE_HOST="172.31.40.234"
MYSQL_SLAVE_PORT="3306"
MYSQL_MASTER_STATUS="OK"
MYSQL_SLAVE_STATUS="OK"
MYSQL_SYNC_STATUS="OK"
MYSQL_MASTER_GTID="4F222222-2222-2222-2222-222222222222:2576"
MYSQL_SLAVE_GTID="4F222222-2222-2222-2222-222222222222:2576"
MYSQL_CHECK_TIMESTAMP="2026-05-25 12:00:00"

REDIS_MASTER_HOST="172.31.40.234"
REDIS_MASTER_PORT="6379"
REDIS_SLAVE_HOST="172.31.40.233"
REDIS_SLAVE_PORT="6379"
REDIS_MASTER_STATUS="OK"
REDIS_SLAVE_STATUS="OK"
REDIS_REPLICATION_STATUS="OK"
REDIS_REPLICATION_DETAIL="link=up io=9 host=172.31.181.148"
REDIS_CHECK_TIMESTAMP="2026-05-25 12:00:00"

LB_HOST="172.31.0.10"
LB_PING="3ms"
LB_PING_STATUS="OK"
LB_CHECK="HAProxy active, backend 3/3 up"
LB_CHECK_TIMESTAMP="2026-05-25 12:00:00"

DNS_HOST="172.31.0.53"
DNS_PING="2ms"
DNS_PING_STATUS="OK"
DNS_CHECK="resolv OK: cluster.local"
DNS_CHECK_TIMESTAMP="2026-05-25 12:00:00"

NC1_HOST="172.31.1.11"
NC1_PING="5ms"
NC1_PING_STATUS="OK"
NC1_CHECK="HTTP 200 /status: maintenance=false"
NC1_CHECK_TIMESTAMP="2026-05-25 12:00:00"

NC2_HOST="172.31.1.12"
NC2_PING="5ms"
NC2_PING_STATUS="OK"
NC2_CHECK="HTTP 200 /status: maintenance=false"
NC2_CHECK_TIMESTAMP="2026-05-25 12:00:00"

NFS_HOST="172.31.2.20"
NFS_PING="1ms"
NFS_PING_STATUS="OK"
NFS_CHECK="mount OK, rw, 1.2T free"
NFS_CHECK_TIMESTAMP="2026-05-25 12:00:00"
```

---

## Roadmap

- [x] Redesigned animated TUI (new-gui branch)
- [x] AI analysis panel
- [x] Animated replication arrows (MariaDB + Redis)
- [x] 11 themes
- [ ] Merge to `main` after field testing

---

## License

MIT License — see [LICENSE](LICENSE).

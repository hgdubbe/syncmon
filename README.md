# SyncMon Monitoring Suite — Experimental Branch

> **⚠️ You are on the `experimental` branch.**
> This branch contains active development work and may be unstable.
> For the stable release, switch to the [`main`](https://github.com/hgdubbe/syncmon/tree/main) branch.

**SyncMon** is a lightweight, high-performance monitoring suite for a Nextcloud high-availability stack. It monitors MariaDB/MySQL and Redis replication clusters and provides a full-stack TUI dashboard covering all infrastructure components.

It consists of two components that work together:

- **`syncmon-daemon`** — A background C service that continuously polls your database and cache nodes, writing real-time replication state to a shared environment file.
- **`syncmon`** — A terminal dashboard (TUI) written in C using `termbox2` that reads the state file and renders a live grid layout covering the entire stack.

---

## What's New in `experimental`

### Grid Layout Dashboard

The TUI has been redesigned from a single-stack view into a **multi-panel grid layout** that maps to the full Nextcloud HA infrastructure:

```
┌─────────────────────────────────────────────────────┐
│                      Overview                       │
├──────────────────────────────────┬──────────────────┤
│           Loadbalancer           │       DNS        │
├──────────────────────┬───────────┴──────────────────┤
│     Nextcloud 1      │         Nextcloud 2           │
├──────────┬───────────┴────────────┬─────────────────┤
│   NFS    │        MariaDB         │      Redis       │
├──────────┴────────────────────────┴─────────────────┤
│                 History / Details                   │
└─────────────────────────────────────────────────────┘
```

| Panel | Data Source | Status |
|---|---|---|
| Overview | Daemon state file | ✅ Live |
| Loadbalancer | — | 🔲 Placeholder |
| DNS | — | 🔲 Placeholder |
| Nextcloud 1 | — | 🔲 Placeholder |
| Nextcloud 2 | — | 🔲 Placeholder |
| NFS | — | 🔲 Placeholder |
| MariaDB | Daemon state file | ✅ Live |
| Redis | Daemon state file | ✅ Live |
| History / Details | Ring buffer | ✅ Live |

Placeholder panels display `Host`, `Reachability`, and `Function` fields as `N/A (not yet configured)` in the theme's dimmed color until live data sources are wired in.

---

## Features

### Dashboard (syncmon)
- **Grid Layout**: Full-stack view of all HA components in a single terminal screen.
- **Real-Time Monitoring**: Tracks MariaDB master/slave status, GTIDs, and Redis replication state.
- **Historical Sparklines**: Visualizes the last 5+ minutes of sync history using Braille or block characters.
- **High-Visibility Alerts**: Errors appear as high-contrast inverted `X` blocks in the timeline.
- **Multiple Themes**: 9 built-in color schemes — Default, Monokai, Dracula, Nord, Gruvbox, Cyberpunk, Calm, White Paper, Grayscale.
- **Test Mode**: `--test` flag simulates live data without a database connection.
- **CLI Arguments**: Refresh interval and state file path can be set at launch without any config file.

### Daemon (syncmon-daemon)
- **Zero External Dependencies**: Statically compiled binary.
- **Universal Compatibility**: Uses standard POSIX C and delegates queries to existing `mysql` and `redis-cli` binaries.
- **System Paths**: Config in `/etc/syncmon.d/config.conf`; logs and state in `/var/log/syncmon/`.
- **Built-In Log Rotation**: Automatically manages log files based on configurable size thresholds.
- **Simulation Mode**: `--test` flag generates mock data without requiring live databases.

---

## Quick Install (Experimental)

> Installs from the `experimental` branch. Do **not** use this in production.

```bash
sudo bash -c "$(curl -fsSL https://raw.githubusercontent.com/hgdubbe/syncmon/experimental/install.sh)"
```

The installer will:
1. Clone the `experimental` branch of the repository.
2. Check for `mysql` and `redis-cli` in `$PATH`.
3. Optionally recompile the daemon and/or TUI from source.
4. Install `syncmon-daemon` and `syncmon` to `/usr/bin/`.
5. Install the config template to `/etc/syncmon.d/config.conf`.
6. Register and optionally start/enable a `systemd` service.

For the stable installer, use the `main` branch:

```bash
sudo bash -c "$(curl -fsSL https://raw.githubusercontent.com/hgdubbe/syncmon/main/install.sh)"
```

---

## Manual Installation

### Prerequisites

- GCC compiler (`build-essential`)
- `mysql` / `mariadb-client` and `redis-tools` available in `$PATH`
- [`termbox2`](https://github.com/termbox/termbox2) single-header library (bundled in repo)

### Build

```bash
git clone --branch experimental https://github.com/hgdubbe/syncmon.git
cd syncmon

# Compile the daemon (static)
gcc -O3 -Wall -static syncmon-daemon.c -o syncmon-daemon

# Compile the TUI
gcc -O2 -march=x86-64 syncmon.c -o syncmon
```

### Install

```bash
sudo install -m 0755 syncmon-daemon /usr/bin/syncmon-daemon
sudo install -m 0755 syncmon        /usr/bin/syncmon

sudo mkdir -p /etc/syncmon.d /var/log/syncmon
sudo install -m 0640 ressources/syncmon.conf /etc/syncmon.d/config.conf

# Edit config before starting the daemon
sudo nano /etc/syncmon.d/config.conf

# Install and start the systemd service
sudo install -m 0644 syncmon-daemon.service /etc/systemd/system/syncmon-daemon.service
sudo systemctl daemon-reload
sudo systemctl enable --now syncmon-daemon
```

---

## Configuration

The daemon reads its configuration exclusively from `/etc/syncmon.d/config.conf`.
The TUI requires **no** configuration file — all options are passed as command-line arguments.

### `/etc/syncmon.d/config.conf` (daemon)

| Key                      | Default                                   | Description                                      |
|--------------------------|-------------------------------------------|--------------------------------------------------|
| `CHECK_INTERVAL`         | `30`                                      | Poll interval in seconds                         |
| `ENABLE_MYSQL_CHECK`     | `1`                                       | Enable MySQL/MariaDB checks (`0` to disable)     |
| `ENABLE_REDIS_CHECK`     | `1`                                       | Enable Redis checks (`0` to disable)             |
| `STARTUP_CHECK`          | `1`                                       | Run a check immediately on daemon start          |
| `EXIT_ON_STARTUP_FAILURE`| `0`                                       | Exit if startup check fails                      |
| `LOG_FILE`               | `/var/log/syncmon/syncmon.log`            | Daemon log file path                             |
| `LOG_MAX_SIZE`           | `100`                                     | Max log size in MB before rotation               |
| `STATE_FILE`             | `/var/log/syncmon/syncmon_state.env`      | State file path (read by TUI)                    |
| `MYSQL_MASTER_HOST`      | `127.0.0.1`                               | MySQL master host                                |
| `MYSQL_MASTER_PORT`      | `3306`                                    | MySQL master port                                |
| `MYSQL_SLAVE_HOST`       | `127.0.0.2`                               | MySQL slave host                                 |
| `MYSQL_SLAVE_PORT`       | `3306`                                    | MySQL slave port                                 |
| `MYSQL_USER`             |                                           | MySQL user for health queries                    |
| `MYSQL_PASSWORD`         |                                           | MySQL password (leave empty if none)             |
| `REDIS_MASTER_HOST`      | `127.0.0.1`                               | Redis master host                                |
| `REDIS_MASTER_PORT`      | `6379`                                    | Redis master port                                |
| `REDIS_SLAVE_HOST`       | `127.0.0.2`                               | Redis slave host                                 |
| `REDIS_SLAVE_PORT`       | `6379`                                    | Redis slave port                                 |
| `REDIS_PASSWORD`         |                                           | Redis password (leave empty if none)             |

---

## Usage

### Service Management

```bash
service syncmon-daemon start    # start the daemon
service syncmon-daemon stop     # stop the daemon
service syncmon-daemon status   # show current status
service syncmon-daemon enable   # enable on boot
service syncmon-daemon disable  # disable on boot
```

Or with systemctl directly:

```bash
systemctl start   syncmon-daemon
systemctl stop    syncmon-daemon
systemctl status  syncmon-daemon
systemctl enable  syncmon-daemon
```

### TUI Dashboard

```bash
syncmon                          # launch with defaults
syncmon -r 5                     # refresh every 5 seconds
syncmon -f /custom/state.env     # use a custom state file
syncmon --test                   # run with simulated data (no daemon required)
syncmon --no-braille             # use classic block graphs
```

### Command Line Reference

| Argument              | Component | Description                                                   |
|-----------------------|-----------|---------------------------------------------------------------|
| `-r`, `--refresh <n>` | TUI       | Refresh interval in seconds (default: `2`)                    |
| `-f`, `--file <path>` | TUI       | Path to the state file (default: `/var/log/syncmon/syncmon_state.env`) |
| `--test`              | Both      | Run with simulated mock data (no live database required)      |
| `--no-braille`        | TUI       | Start with classic block graphs instead of Braille            |
| `--help`, `-h`        | Both      | Show the help message                                         |

### TUI Keyboard Controls

| Key            | Action                                                  |
|----------------|---------------------------------------------------------|
| `q` / `Ctrl+C` | Quit                                                    |
| `t`            | Open theme selection menu (navigate with ↑/↓ + Enter)  |
| `g`            | Toggle graph style between Braille and classic blocks   |

---

## File Locations

| Path                                    | Description                        |
|-----------------------------------------|------------------------------------|
| `/usr/bin/syncmon-daemon`               | Daemon binary                      |
| `/usr/bin/syncmon`                      | TUI binary                         |
| `/etc/syncmon.d/config.conf`            | Daemon configuration               |
| `/etc/systemd/system/syncmon-daemon.service` | systemd unit file             |
| `/var/log/syncmon/syncmon.log`          | Daemon log (auto-rotated)          |
| `/var/log/syncmon/syncmon_state.env`    | Live state file (daemon → TUI)     |

---

## State File Reference

The daemon writes to `/var/log/syncmon/syncmon_state.env`. This is the interface contract between the daemon and the TUI.

```ini
OVERALL_STATUS="OK"
SYNCMON_TIMESTAMP="2026-05-24 09:30:00"
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
MYSQL_CHECK_TIMESTAMP="2026-05-24 09:30:00"

REDIS_MASTER_HOST="172.31.40.234"
REDIS_MASTER_PORT="6379"
REDIS_SLAVE_HOST="172.31.40.233"
REDIS_SLAVE_PORT="6379"
REDIS_MASTER_STATUS="OK"
REDIS_SLAVE_STATUS="OK"
REDIS_REPLICATION_STATUS="OK"
REDIS_REPLICATION_DETAIL="link=up io=9 host=172.31.181.148"
REDIS_CHECK_TIMESTAMP="2026-05-24 09:30:00"
```

---

## Roadmap (experimental → main)

- [ ] Live data for Loadbalancer panel (HAProxy stats socket / HTTP health endpoint)
- [ ] Live data for DNS panel (dig/nslookup reachability probe)
- [ ] Live data for Nextcloud 1 / 2 panels (HTTP status check)
- [ ] Live data for NFS panel (mount reachability / `showmount` check)
- [ ] Per-panel `env` keys in daemon and state file
- [ ] Merge to `main` once all panels have live data

---

## License

This project is open-source and available under the [MIT License](LICENSE).

# SyncMon Monitoring Suite

**SyncMon** is a lightweight, high-performance monitoring suite for MariaDB/MySQL and Redis replication clusters. It consists of two components that work together:

- **`syncmon-daemon`** — A background service written in C that continuously polls your database and cache nodes, writing real-time replication state to a shared environment file.
- **`syncmon`** — A terminal dashboard (TUI) written in C using `termbox2` that reads the state file and renders live replication health, GTID positions, and historical sparklines directly in your terminal.



---

## Features

### Dashboard (syncmon)
- **Real-Time Monitoring**: Tracks MariaDB master/slave status, GTIDs, and Redis replication state.
- **Historical Sparklines**: Visualizes the last 5+ minutes of sync history using Braille or block characters.
- **High-Visibility Alerts**: Errors immediately stand out as high-contrast inverted `X` blocks in the timeline.
- **Multiple Themes**: 9 built-in color schemes — Default, Monokai, Dracula, Nord, Gruvbox, Cyberpunk, Calm, White Paper, Grayscale.
- **Test Mode**: Built-in `--test` flag to simulate live data without a database connection.

### Daemon (syncmon-daemon)
- **Zero External Dependencies**: Statically compiled binary
- **Universal Compatibility**: Uses standard POSIX C and delegates queries to existing `mysql` and `redis-cli` binaries.
- **Real-Time State Output**: Continuously writes replication lag, GTID positions, and node statuses to the state file.
- **Built-In Log Rotation**: Automatically manages log files based on configurable size thresholds.
- **Simulation Mode**: `--test` flag generates mock data without requiring live databases.

---

## Installation

Both components share the same repository and configuration directory.

### Prerequisites
- GCC compiler
- `mysql` and `redis-cli` binaries available in `$PATH` (required by the daemon)
- [`termbox2`](https://github.com/termbox/termbox2) single-header library (required by the TUI)

### Build

1. Clone the repository:
   ```bash
   git clone https://github.com/yourusername/syncmon.git
   cd syncmon
   ```

2. Download `termbox2` (if not already included):
   ```bash
   wget https://raw.githubusercontent.com/termbox/termbox2/master/termbox2.h
   ```

3. Compile the TUI dashboard:
   ```bash
   gcc -O2 -march=x86-64 -o syncmon syncmon.c
   ```

4. Compile the daemon:
   ```bash
   gcc -O3 -Wall -static syncmon-daemon.c -o syncmon-daemon
   ```

---

## Configuration

Both components share a single configuration file located at `./ressources/syncmon.conf` relative to each executable.

### Environment Variables (TUI override)

| Variable         | Description                                                        |
|------------------|--------------------------------------------------------------------|
| `DISPLAY_REFRESH`| Polling interval in seconds for reading the state file (default: 2)|
| `STATE_FILE`     | Custom path to the state environment file                          |
| `USE_BRAILLE`    | Set to `0` to disable Braille graphs by default                   |

---

## Usage

### 1. Start the Daemon

Run in the background via `systemd`, `tmux`, or `nohup`:
```bash
./syncmon-daemon
```

### 2. Start the TUI Dashboard

```bash
./syncmon
```

### Command Line Options

| Argument       | Component | Description                                               |
|----------------|-----------|-----------------------------------------------------------|
| `--test`       | Both      | Run with simulated mock data (no live database required). |
| `--no-braille` | TUI       | Start with classic block graphs instead of Braille.       |
| `--help`, `-h` | Both      | Show the help message.                                    |

### TUI Keyboard Controls

| Key            | Action                                                  |
|----------------|---------------------------------------------------------|
| `q` / `Ctrl+C` | Quit the application                                    |
| `t`            | Open theme selection menu (navigate with ↑/↓ + Enter)  |
| `g`            | Toggle graph style between Braille and classic blocks   |

---

## State File Reference

The daemon writes to `/tmp/syncmon_state.env` (or the path set by `STATE_FILE`). This is the interface contract between the two components.

```ini
OVERALL_STATUS="OK"
SYNCMON_TIMESTAMP="2026-05-23 20:44:54"
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
MYSQL_CHECK_TIMESTAMP="2026-05-23 20:44:54"

REDIS_MASTER_HOST="172.31.40.234"
REDIS_MASTER_PORT="6379"
REDIS_SLAVE_HOST="172.31.40.233"
REDIS_SLAVE_PORT="6379"
REDIS_MASTER_STATUS="OK"
REDIS_SLAVE_STATUS="OK"
REDIS_REPLICATION_STATUS="OK"
REDIS_REPLICATION_DETAIL="link=up io=9 host=172.31.181.148"
REDIS_CHECK_TIMESTAMP="2026-05-23 20:44:54"
```

---

## License

This project is open-source and available under the [MIT License](LICENSE).

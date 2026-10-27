<p align="center">
  <img src="logo.png" alt="axil-nd logo" width="220" />
</p>

# axil-nd

[![C99](https://img.shields.io/badge/C-C99-555?logo=c)](#)
[![BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![MUCK](https://img.shields.io/badge/game-MUCK-4B8BBE)](#)

> NeverDark MUCK engine as a dynamic [axil](https://github.com/tty-pt/axil) module — real-time multiplayer virtual world running in-process over WebSocket and Telnet.

---

## 1. Quickstart

Launch NeverDark MUCK inside the Axil server:

```bash
mkdir -p var
AXIL_ND_DB=./var/std.db axil -d -A -p 8080 -m ./lib/libaxil-nd:libnd-region:libnd-bus:libnd-obj:libnd-world:libnd-view:libnd-mcp:libnd-speech:libnd-session
```

### Connect to the World
- **Web Browser (Xterm 6 + Canvas Viewport)**: Navigate to `http://localhost:8080/nd`
- **Raw Telnet / Netcat**:
  ```bash
  nc 127.0.0.1 8080
  connect <your-unix-username>
  ```

---

## 2. In-Game Command Cheat Sheet

NeverDark uses clean, natural command verbs without legacy `@` prefixes:

| Category | Command | Syntax & Example | What It Does |
| :--- | :--- | :--- | :--- |
| **World Shaping** | `create` | `create sword` / `create table` | Spawns a new in-game item or object |
| | `door` | `door north to room_12` | Carves a navigable exit passage between rooms |
| | `undoor` | `undoor north` | Removes an exit passage |
| | `wall` | `wall north` | Erects a structural barrier blocking passage |
| | `unwall` | `unwall north` | Demolishes a barrier, opening the space |
| | `claim` / `unclaim` | `claim` | Claims/releases stewardship of current district |
| | `recycle` | `recycle debris` | Destroys an object and returns database quota |
| **Exploration** | `look` (`l`) | `look` / `look statue` | Describes current room or item |
| | `walk` | `walk north` (or `n`, `s`, `e`, `w`, `u`, `d`) | Navigates through room exits |
| | `sit` / `stand` | `sit chair` / `stand` | Interacts with furniture objects (`nd-seat`) |
| **Communication** | `say` (`"`) | `say Greetings, traveler!` | Speaks aloud to everyone present in the room |
| | `pose` (`:`) | `:nods thoughtfully` | Emotes a physical action in third person |
| | `page` | `page alice=Are you ready?` | Sends a private whisper directly to a player |
| **RPG & Combat** | `status` / `score` | `status` / `score` | Displays health, mana, stamina, and attributes |
| | `inventory` (`i`) | `inventory` | Lists items currently carried in your inventory |
| | `equip` | `equip iron_sword` | Wields weapons or wears armor (`nd-equip`) |
| | `fight` | `fight goblin` | Initiates combat encounter (`nd-fight`) |
| | `cast` | `cast spark on goblin` | Casts an incantation from your spellbook (`nd-spell`) |
| **Administration** | `loadmod` | `loadmod nd-spell` | Dynamically loads a C module into a region (`xy_load`) |
| | `streload` | `streload` | Hot-reloads in-memory Corm database state |

---

## 3. Architecture & Subsystems

```text
┌─────────────────────────────────────────────────────────────────┐
│                          Axil Daemon                            │
│    WebSocket Upgrade (GET /nd)            Raw Telnet Port       │
└────────────────────────────────┬────────────────────────────────┘
                                 │
┌────────────────────────────────▼────────────────────────────────┐
│                        axil-nd Core                             │
│  ┌──────────────────────┐             ┌──────────────────────┐  │
│  │     libcorm Maps     │             │ libislet (3D Space)  │  │
│  │   Objects & Props    │             │  Morton Coordinates  │  │
│  └──────────────────────┘             └──────────────────────┘  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │ Content Plugins (libxylem): nd-fight, nd-spell, nd-biome  │  │
│  └───────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

The game world is extended through independent, hot-reloadable [libxylem](https://github.com/tty-pt/libxylem) modules:
- **Core Entities**: `nd-core` (object trees), `nd-attr` (statistics), `nd-class` (character classes), `nd-race` (species traits).
- **Environment**: `nd-biome` (climates & weather), `nd-plant` (flora & growth), `nd-wts` (world temporal state & day/night).
- **Gameplay**: `nd-fight` (tactical combat), `nd-spell` (magic incantations), `nd-shop` (in-game merchants), `nd-seat` (resting).

---

## 4. Build from Source

```bash
# Build browser client assets
npm install && npm run build

# Build native C module
make
sudo make install
```

## License

BSD 2-Clause License. Copyright (c) 2026, quirinpa. See `LICENSE`.

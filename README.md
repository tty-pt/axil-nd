<p align="center">
  <img src="logo.png" alt="axil-nd logo" width="220" />
</p>

# axil-nd

[![C99](https://img.shields.io/badge/C-C99-555?logo=c)](#)
[![BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![MUCK](https://img.shields.io/badge/game-MUCK-4B8BBE)](#)

> NeverDark MUCK engine as a dynamic [axil](https://github.com/tty-pt/axil)
> module — the game runs in-process behind axil's WebSocket/telnet server.

A port of the [NeverDark](https://github.com/tty-pt/neverdark) MUCK (a
rewrite of TinyMUCK/FuzzBall) built on the axil HTTP/server stack and the
corm database:

- Game engine + region-modded content modules in-process (`loadmod`/
  `unloadmod` into persisted `st` rows, restored every boot; `grant`
  attributes any region — districts, planets, super-planet regions — to any
  player)
- Binary Client Protocol (BCP `#b` magic frames) and WebSocket channel multiplexing
- Multi-viewport rendering: 3D WebGL isometric scenes, 2D Lamina canvas, pure-C Bud DOM, and ANSI TTY
- Store ported from the legacy `qdb`/`qmap` API to `libcorm`
- Extensible UI slots and declarative tenant DSV tables (`nd_table_load`)
- Browser client (xterm 6) plus `art/` and the in-game `man/` help pages,
  all installed under `share/axil-nd`

## Architecture & Subsystems

```
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

The game world is extended through independent, hot-reloadable libxylem modules:
- **Core Character Systems**:
  - [`nd-core`](https://github.com/tty-pt/nd-core) — Foundational object tree, containers, locations, and property lists.
  - [`nd-attr`](https://github.com/tty-pt/nd-attr) — Core physical and mental statistics (Strength, Dexterity, Intellect, Willpower).
  - [`nd-class`](https://github.com/tty-pt/nd-class) — Character classes, archetypes, and specialized skill progression trees.
  - [`nd-race`](https://github.com/tty-pt/nd-race) — Species traits, racial abilities, and physical characteristics.
  - [`nd-level`](https://github.com/tty-pt/nd-level) — Experience gain, leveling curves, and stat scaling.
  - [`nd-mortal`](https://github.com/tty-pt/nd-mortal) — Mortality, damage tracking, death states, and resurrection shrines.
- **World Simulation & Environment**:
  - [`nd-biome`](https://github.com/tty-pt/nd-biome) — Dynamic climate systems, ambient weather, and terrain types.
  - [`nd-plant`](https://github.com/tty-pt/nd-plant) — Botanical ecosystems, forageable herbs, and plant growth cycles.
  - [`nd-stone`](https://github.com/tty-pt/nd-stone) — Mining veins, minerals, gems, and geological structures.
  - [`nd-seat`](https://github.com/tty-pt/nd-seat) — Interactive chairs, benches, and resting mechanics.
  - [`nd-wts`](https://github.com/tty-pt/nd-wts) — World Temporal State: synchronized astronomical clocks and day/night cycles.
- **Gameplay, Combat & Economy**:
  - [`nd-fight`](https://github.com/tty-pt/nd-fight) — Turn-based and real-time tactical combat engine.
  - [`nd-spell`](https://github.com/tty-pt/nd-spell) — Spellbook management, casting incantations, and elemental effects.
  - [`nd-equip`](https://github.com/tty-pt/nd-equip) — Equipment slots, encumbrance, and weapon stat modifiers.
  - [`nd-shop`](https://github.com/tty-pt/nd-shop) — In-game merchants, currency transactions, and bartering.
  - [`nd-drink`](https://github.com/tty-pt/nd-drink) — Tavern drinks, potions, and thirst simulation.
  - [`nd-mob`](https://github.com/tty-pt/nd-mob) — Non-player characters, monster AI, and roaming behaviors.
  - [`nd-vanilla`](https://github.com/tty-pt/nd-vanilla) — Standard classic MUCK building and utility commands.

## In-Game Command Cheat Sheet

NeverDark uses clean, natural command verbs without legacy `@` prefixes:

| Category | Command | Syntax & Example | What It Does |
| :--- | :--- | :--- | :--- |
| **World Shaping** | `create` | `create sword` / `create table` | Spawns a new in-game item or object |
| | `door` | `door north to room_12` | Carves a navigable exit passage between rooms |
| | `undoor` | `undoor north` | Removes an exit passage |
| | `wall` | `wall north` | Erects a structural barrier blocking passage |
| | `unwall` | `unwall north` | Demolishes a barrier, opening the space |
| | `claim` / `unclaim` | `claim` | Claims/releases stewardship of the current district |
| | `chown` | `chown sword=alice` | Transfers ownership of an object to another player |
| | `clone` | `clone chair` | Creates an exact replica of an existing object |
| | `recycle` | `recycle debris` | Destroys an object and returns database quota |
| **Exploration** | `look` (`l`) | `look` / `look statue` | Describes the current room or an item in it |
| | `examine` (`x`) | `examine chest` | Inspects detailed attributes and internal properties |
| | `walk` | `walk north` (or `n`, `s`, `e`, `w`, `u`, `d`) | Navigates through room exits and terrain |
| | `sit` / `stand` | `sit chair` / `stand` | Interacts with furniture objects (`nd-seat`) |
| **Communication** | `say` (`"`) | `say Greetings, traveler!` | Speaks aloud to everyone present in the room |
| | `pose` (`:`) | `:nods thoughtfully` | Emotes a physical action in third person |
| | `page` | `page alice=Are you ready?` | Sends a private whisper directly to a player |
| **RPG & Combat** | `status` / `score` | `status` / `score` | Displays health, mana, stamina, and attributes |
| | `inventory` (`i`) | `inventory` | Lists items currently carried in your inventory |
| | `equip` / `unequip` | `equip iron_sword` | Wields weapons or wears armor (`nd-equip`) |
| | `fight` | `fight goblin` | Initiates combat encounter (`nd-fight`) |
| | `cast` | `cast spark on goblin` | Casts an incantation from your spellbook (`nd-spell`) |
| | `shop` / `buy` / `sell` | `shop` / `buy potion` | Trades with merchant NPCs (`nd-shop`) |
| **Administration** | `loadmod` | `loadmod nd-spell` | Dynamically loads a C module into a region (`xy_load`) |
| | `unloadmod` | `unloadmod nd-spell` | Safely unloads a C module from a region |
| | `streload` | `streload` | Hot-reloads in-memory Corm database state |

## Status

Playable. The engine port is complete and the tree is self-contained: it
builds, tests and runs from a fresh clone with no other checkout required.

## Build from source

The module builds with a plain `make` (the shared [`mk` include.mk]
(https://github.com/tty-pt/mk) is expected as a sibling directory, as is
`axil-tty` for its headers and `libaxil-tty`).

The browser client is **generated** and gitignored, so build it first — a
plain `make` alone leaves `htdocs/` empty and the client 404s:

```sh
npm install           # install build deps
npm run build         # generates htdocs/ (the client)
make                  # builds lib/libaxil-nd.so and generates man/
make test             # run the in-tree test suite (./test.sh)
sudo make install     # module + htdocs + art + man -> $(PREFIX)/share/axil-nd
```

**Dependencies:** `libaxil`, `libcorm`, `libxylem`, `libislet`, `libqsys` and
`axil-tty` (from the tty.pt repo) provide the headers and libraries. All six
are required; the CI dependency list must match `LDLIBS` in the `Makefile`.

## Quick start

The module resolves `art/`, `htdocs/`, `man/` and `motd` through `nd_asset_dir()`,
checking in order:
1. Per-class env `AXIL_ND_<CLASS>` (`AXIL_ND_HTDOCS`, `AXIL_ND_ART`, `AXIL_ND_MAN`, `AXIL_ND_MOTD`);
2. Base directory `AXIL_ND_DATADIR/<dir>`;
3. Compiled `$(PREFIX)/share/axil-nd/<dir>`;
4. Submodule relative path `external/axil-nd/<dir>`;
5. Working directory `<dir>`.

Run it from the checkout root, parent site checkout, or installed `share/axil-nd`:

```sh
axil -d -A -p 8080 -m ./lib/libaxil-nd.so
```

Then play at <http://localhost:8080/nd>, or over telnet:

```sh
nc 127.0.0.1 8080
connect <your-unix-username>
```

The guest name must be a real account on the host — authentication calls
`getpwnam()` and fails otherwise. Over raw telnet, terminate every command
with a blank line, as the reader only dispatches a complete head.

On a development host, point the store somewhere writable:

```sh
mkdir -p var
AXIL_ND_DB=./var/std.db axil -d -A -p 8080 -m ./lib/libaxil-nd.so
```

## The man pages

The 72 in-game help pages (`help <topic>`) are tracked as `man-src/*.10` and
copied into `man/` by the `man` target, which `all` depends on. `man/` is
gitignored build output and is resolved relative to the working directory at
runtime, exactly as `art/` and `htdocs/` are. A bare `help` (or `man`) lists
every installed topic. The editable login notice is the `motd` file, read
from the working directory at login and installed to the share directory by
`install-data`.

## License

BSD 2-Clause License. Copyright (c) 2026, quirinpa. See `LICENSE`, which also
carries the TinyMUCK/FuzzBall GPL lineage note. Components retained from the
original TinyMUCK/FuzzBall codebase remain under the GPLv2; the inherited
`GPL.txt` and `docs/COPYING` are included verbatim in this repository.

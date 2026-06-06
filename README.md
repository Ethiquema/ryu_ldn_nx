# ryu_ldn_nx

[![Build Status](https://github.com/Ethiquema/ryu_ldn_nx/actions/workflows/build.yml/badge.svg)](https://github.com/Ethiquema/ryu_ldn_nx/actions)
[![Coverage](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/Pikatsuto/4277995494c6874ebb238bdbc499d9d0/raw/coverage.json)](https://github.com/Ethiquema/ryu_ldn_nx/actions)
[![License: GPL v2](https://img.shields.io/badge/License-GPLv2-blue.svg)](https://www.gnu.org/licenses/gpl-2.0)

Nintendo Switch sysmodule enabling online multiplayer via Ryujinx LDN servers — no complex network setup required.


## 🚧 Work in Progress

This project is still under development and is not ready for release yet.

If you want to test the project and contribute, please use a private RyuLDN server, please DO NOT USE the official Ryujinx LDN servers with this project (ldn.ryujinx.app).

A hosted testing instance is also available via IP address at ryuldnnx.ddns.net.

## What is ryu_ldn_nx?

ryu_ldn_nx is an Atmosphere sysmodule that intercepts local wireless (LDN) game traffic and routes it through Ryujinx's LDN servers over TCP. This allows Switch users with CFW to play online multiplayer games without:

- Complex LAN play configurations
- PC with pcap in connection sharing mode
- Manual network setup

Just install, connect to WiFi, and play!

## How It Works

ryu_ldn_nx registers **three IPC services** simultaneously:

1. **`ldn:u` MITM** — Intercepts Nintendo's LDN user service. Implements `ICommunicationService` with a full state machine (None → Initialized → AccessPoint/Station → AccessPointCreated/StationConnected). Games see synthesized `NetworkInfo` and node IDs from Ryujinx server responses.

2. **`bsd:u` MITM** — Intercepts BSD socket calls. Sockets that `bind`/`connect` to the LDN subnet `10.114.x.x` are tracked as proxy sockets and their traffic is tunneled through `ProxyData` packets to the Ryujinx server. This is how gameplay UDP/TCP traffic (PIA mesh protocol) reaches other peers without pcap.

3. **`ryu:cfg`** — Custom IPC service the Tesla overlay talks to for live configuration and status display.

## Planned Features

- **Plug and Play**: Install like any sysmodule, no configuration required
- **Automatic Connection**: Connects to Ryujinx servers automatically
- **All LDN Games**: Works with any game supporting local wireless multiplayer
- **Tesla Overlay**: Live status, configuration, and connection management
- **P2P Support**: Optional direct peer-to-peer via UPnP to reduce server load

## Installation

### Requirements

- Nintendo Switch with Atmosphere CFW (1.0.0+)
- Internet connection (WiFi)
- SD card with Atmosphere installed

### Quick Install

1. Download the latest release from [Releases](https://github.com/Ethiquema/ryu_ldn_nx/releases)
2. Extract the ZIP to your SD card root (contains `/atmosphere/contents/4200000000000010/` layout)
3. Reboot your Switch
4. Launch any game with local multiplayer — it will automatically use online mode!

### Configuration (Optional)

Create `sdmc:/config/ryu_ldn_nx/config.ini` (or let the sysmodule auto-create it on first boot):

```ini
[server]
host = ryuldnnx.ddns.net    ; IP address or hostname
port = 30456
use_tls = 0                      ; NOT IMPLEMENTED — no TLS code exists

[network]
connect_timeout = 5000
reconnect_delay = 3000
max_reconnect_attempts = 5       ; 0 = disable auto-reconnect

[ldn]
enabled = 1
disable_p2p = 1

[debug]
enabled = 0
level = 1
log_to_file = 0
```

All settings have sensible defaults and can be changed live via the Tesla overlay (`ryu:cfg` service).

## Supported Games

Any game that supports Nintendo's Local Wireless (LDN) multiplayer should work, including:

- Mario Kart 8 Deluxe
- Super Smash Bros. Ultimate
- Splatoon 2 / 3
- Monster Hunter Rise
- Animal Crossing: New Horizons
- Pokémon Sword / Shield
- And many more!

## Development

### Building from Source

#### Using Docker (Recommended)

```bash
git clone --recursive https://github.com/Ethiquema/ryu_ldn_nx.git
cd ryu_ldn_nx

# Build everything (sysmodule + overlay + dist ZIP)
docker compose run --rm build

# Run host unit tests
docker compose run --rm test

# Clean build artifacts and output/
docker compose run --rm clean

# Package for distribution
cd sysmodule && make dist
```

The build produces:
- `output/` directory with complete SD card structure
- `ryu_ldn_nx-release.zip` with the same layout

#### Per-suite test targets

```bash
cd tests && make test-ldn-state-machine       # see Makefile for full list
make COVERAGE=1 coverage                        # gcov report
```

Available suites: `protocol`, `config`, `config-manager`, `log`, `socket`, `tcp-client`, `connection-state`, `reconnect`, `client`, `ldn-types`, `ldn-state-machine`, `ldn-proxy`, `ldn-error`, `ldn-integration`, `overlay`, `ipc-config`, `config-ipc-service`, `shared-state`, `packet-dispatcher`, `session-handler`, `proxy-handler`, `handler-integration`, `upnp`, `p2p-proxy`, `p2p-client`, `p2p-integration`, `p2p-create-network`.

#### Native Build

Requirements:
- [devkitPro](https://devkitpro.org/wiki/Getting_Started) with devkitA64
- libnx
- Atmosphere libraries (libstratosphere)

```bash
dkp-pacman -S switch-dev libnx
git clone --recursive https://github.com/Atmosphere-NX/Atmosphere.git
cd Atmosphere && make -C libraries

export ATMOSPHERE_LIBS=/path/to/Atmosphere/libraries
cd /path/to/ryu_ldn_nx/sysmodule && make
```

### Project Structure

```
ryu_ldn_nx/
├── sysmodule/           # Main sysmodule source
│   ├── source/
│   │   ├── ldn/         # LDN MITM service, state machine, ICommunicationService
│   │   ├── bsd/         # BSD MITM service, ProxySocket, ProxySocketManager
│   │   ├── network/     # TCP client, connection state, reconnect manager
│   │   ├── protocol/    # Wire format (binary-compatible with C# server)
│   │   ├── p2p/         # P2P proxy client/server, UPnP
│   │   ├── config/      # INI parser, ConfigManager, IPC service
│   │   ├── debug/       # File logger, circular buffer
│   │   └── main.cpp     # Entry point, heap, service registration
│   └── Makefile
├── overlay/             # Tesla overlay (libultrahand)
├── tests/               # Host unit tests
├── scripts/             # Build scripts, GDB debugger scripts
├── config/              # Example configuration
└── docs/                 # Astro + Starlight documentation site
```

### Debugging on Hardware

```bash
# Interactive GDB session
docker compose run --rm debugger <SWITCH_IP> [PID]

# Regenerate GDB tracepoints from @gdb{} annotations
python3 scripts/gdb_codegen.py generate
python3 scripts/gdb_codegen.py verify
```

Log files on Switch: `config/ryu_ldn_nx/ryu_ldn_nx.log` (when `log_to_file=1`).

## Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting PRs.

### Developer Certificate of Origin (DCO)

All commits must be signed off:

```bash
git commit -s -m "Your commit message"
```

This certifies that you wrote or have the right to submit the code under the project's license.

## License

This project is licensed under the GNU General Public License v2.0 — see [LICENSE](LICENSE) for details.

## Credits

- [ldn_mitm](https://github.com/spacemeowx2/ldn_mitm) — Original Switch LDN implementation
- [Ryujinx](https://git.ryujinx.app/ryubing/ryujinx) — LDN server and protocol
- [Atmosphere](https://github.com/Atmosphere-NX/Atmosphere) — Switch CFW and libraries
- [libnx](https://github.com/switchbrew/libnx) — Switch homebrew library

## Disclaimer

This software is provided "as is" without warranty. Use at your own risk. This project is not affiliated with Nintendo, Ryujinx, or Atmosphere.
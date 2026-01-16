# ryu_ldn_nx

[![Build Status](https://github.com/Ethiquema/ryu_ldn_nx/actions/workflows/build.yml/badge.svg)](https://github.com/Ethiquema/ryu_ldn_nx/actions)
[![License: GPL v2](https://img.shields.io/badge/License-GPLv2-blue.svg)](https://www.gnu.org/licenses/gpl-2.0)

Nintendo Switch sysmodule enabling online multiplayer via Ryujinx LDN servers - no complex network setup required.

## What is ryu_ldn_nx?

ryu_ldn_nx is an Atmosphere sysmodule that intercepts local wireless (LDN) game traffic and routes it through Ryujinx's LDN servers. This allows Switch users with CFW to play online multiplayer games without:

- Complex LAN play configurations
- PC with pcap in connection sharing mode
- Manual network setup

Just install, connect to WiFi, and play!

## Features

- **Plug and Play**: Install like any sysmodule, no configuration required
- **Automatic Connection**: Connects to Ryujinx servers automatically
- **All LDN Games**: Works with any game supporting local wireless multiplayer
- **Low Overhead**: Minimal impact on system performance
- **Tesla Overlay**: Optional overlay for status and configuration (coming soon)

## Installation

### Requirements

- Nintendo Switch with Atmosphere CFW (1.0.0+)
- Internet connection (WiFi)
- SD card with Atmosphere installed

### Quick Install

1. Download the latest release from [Releases](https://github.com/Ethiquema/ryu_ldn_nx/releases)
2. Extract the ZIP to your SD card root
3. The files should be placed in:
   ```
   /atmosphere/contents/4200000000000010/
   ```
4. Reboot your Switch
5. Launch any game with local multiplayer - it will automatically use online mode!

### Configuration (Optional)

Create `/config/ryu_ldn_nx/config.ini` to customize settings:

```ini
[server]
host = ldn.ryujinx.app
port = 30456

[network]
connect_timeout = 5000
ping_interval = 10000

[debug]
enabled = 0
level = 1
```

## Supported Games

Any game that supports Nintendo's Local Wireless (LDN) multiplayer should work, including:

- Mario Kart 8 Deluxe
- Super Smash Bros. Ultimate
- Splatoon 2/3
- Monster Hunter Rise
- Animal Crossing: New Horizons
- And many more!

## Development

### Building from Source

#### Using Docker (Recommended)

```bash
# Clone the repository
git clone --recursive https://github.com/Ethiquema/ryu_ldn_nx.git
cd ryu_ldn_nx

# Build sysmodule
docker-compose run --rm build

# Build overlay
docker-compose run --rm overlay

# Run tests
docker-compose run --rm test
```

#### Native Build

Requirements:
- [devkitPro](https://devkitpro.org/wiki/Getting_Started) with devkitA64
- libnx
- Atmosphere libraries (libstratosphere)

```bash
# Install devkitPro packages
dkp-pacman -S switch-dev libnx

# Clone Atmosphere for libraries
git clone --recursive https://github.com/Atmosphere-NX/Atmosphere.git
cd Atmosphere && make -C libraries

# Set environment
export ATMOSPHERE_LIBS=/path/to/Atmosphere/libraries

# Build
cd /path/to/ryu_ldn_nx/sysmodule
make
```

### Project Structure

```
ryu_ldn_nx/
├── sysmodule/           # Main sysmodule source
│   ├── source/          # C++ source files
│   ├── res/             # Resources (app.json)
│   └── Makefile
├── overlay/             # Tesla overlay
├── docs/                # Documentation
├── tests/               # Unit tests
└── config/              # Example configuration
```

## Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting PRs.

### Developer Certificate of Origin (DCO)

All commits must be signed off:

```bash
git commit -s -m "Your commit message"
```

This certifies that you wrote or have the right to submit the code under the project's license.

## License

This project is licensed under the GNU General Public License v2.0 - see [LICENSE](LICENSE) for details.

## Credits

- [ldn_mitm](https://github.com/spacemeowx2/ldn_mitm) - Original Switch LDN implementation
- [Ryujinx](https://git.ryujinx.app/ryubing/ryujinx) - LDN server and protocol
- [Atmosphere](https://github.com/Atmosphere-NX/Atmosphere) - Switch CFW and libraries
- [libnx](https://github.com/switchbrew/libnx) - Switch homebrew library

## Disclaimer

This software is provided "as is" without warranty. Use at your own risk. This project is not affiliated with Nintendo, Ryujinx, or Atmosphere.

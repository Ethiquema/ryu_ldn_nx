# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Nothing yet

### Changed
- Nothing yet

### Fixed
- Nothing yet

## [0.1.0] - 2026-01-12

### Added

#### Infrastructure (Epic 0)
- Docker Compose for build/test/overlay
- GitHub Actions CI/CD workflows (build, test, release, docs)
- Atmosphere-libs submodule integration

#### Protocol (Epic 1)
- RyuLdn protocol encoder/decoder
- PacketBuffer for efficient packet handling
- All protocol types and structures
- 31 unit tests passing

#### Network Client (Epic 2)
- TCP client with BSD sockets
- Connection state machine
- Automatic reconnection with exponential backoff
- 46 unit tests passing

#### LDN Service (Epic 3)
- MITM service for ldn:u interception
- Complete IPC command implementation
- LDN state machine (None → Initialized → AccessPoint/Station → Connected)
- Proxy data buffering and routing
- Node ID mapping
- Error handling and recovery
- 27 integration tests passing

#### Tesla Overlay (Epic 4)
- Connection status display with color coding
- Session information (players, host/client, duration)
- Server address display
- Debug toggle
- Force reconnect button
- Latency (RTT) display

#### Documentation (Epic 5)
- Starlight documentation site
- Installation guide
- Configuration guide
- Overlay usage guide
- Contributing guide
- Architecture documentation
- IPC commands reference
- Protocol specification
- API documentation from source

### Technical Details
- 396 total unit tests
- Doxygen documentation generation
- Automatic release packaging
- GitHub Pages deployment

---

## Version History

Future releases will be documented here following semantic versioning:
- **MAJOR**: Incompatible protocol changes
- **MINOR**: New features, backward compatible
- **PATCH**: Bug fixes, backward compatible

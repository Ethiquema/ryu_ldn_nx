#pragma once

// TODO(MEDIUM): migrate callers (main.cpp, ldn_icommunication.cpp, p2p_*.cpp, tcp_client.cpp, etc.)
//               to use these constants instead of scattered literals. See LINT-30.

/**
 * @file memory_config.hpp
 * @brief Centralized memory-size constants for the ryu_ldn_nx sysmodule
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 *
 * Rationale (LINT-30): memory-size constants were previously scattered as
 * file-local `constexpr` values across main.cpp, ldn_icommunication.cpp,
 * p2p_proxy_client.cpp, p2p_proxy_server.cpp, tcp_client.cpp, etc.
 * Centralising them here gives a single place to audit the sysmodule's
 * total memory budget (~10 MB shared across all Switch sysmodules — see
 * AGENTS.md "Memory Constraints") and prevents accidental drift when a
 * buffer is resized in one file but the related constant in another file
 * is missed.
 *
 * Usage: include this header where a memory-size constant is needed.
 * The constants live in namespace `ryu_ldn::memory` to avoid polluting
 * the global namespace. Where existing code declares a file-local
 * constant with the same value, the file-local declaration can be
 * replaced by `using ryu_ldn::memory::<kConstant>` or by referencing
 * the fully-qualified name. This header intentionally does NOT remove
 * the existing declarations — migration is incremental and each call
 * site should be updated individually to verify the substitution is
 * semantically identical.
 *
 * Values are byte-for-byte identical to the previously-scattered
 * constants; no size is changed by this header, only the location of
 * the definition.
 *
 * Sections:
 *   - Heap / malloc buffers (process-wide dynamic allocation backing)
 *   - Thread stacks (per-thread BSS stacks)
 *   - Socket / network buffers (libnx BSD config + per-call staging)
 *   - IPC server manager limits (sessions, domains, pointer buffers)
 *   - Log subsystem buffers
 *   - LDN/P2P subsystem buffers
 *
 * @note Switch hardware constraints (see AGENTS.md):
 *   - Total sysmodule budget ~10 MB across ALL sysmodules
 *   - 384 KB expanded heap (raised from 96 KB after DABRT 0x101)
 *   - 1 MB malloc buffer for TLS heap central
 *   - Do NOT grow these constants without proof the current size is
 *     insufficient.
 */

#include <cstddef>
#include <cstdint>

namespace ryu_ldn::memory {

// ============================================================================
// Heap / malloc buffers (process-wide)
// ============================================================================
// The expanded heap backs the overridden `new`/`delete` (routed via
// lmem::ExpHeap). 384 KB covers: game whitelist (~40 KB), proxy socket
// receive queues (up to ~45 KB under lobby traffic), pending-packet
// buffer, ExpHeap overhead, and transient allocations. 96 KB saturated
// under real gameplay and caused DABRT 0x101 on allocation failure.
inline constexpr size_t HEAP_SIZE = 384 * 1024;          ///< 384 KB expanded heap
inline constexpr size_t HEAP_ALIGNMENT = 0x40;           ///< 64-byte alignment for ExpHeap
inline constexpr size_t MALLOC_BUFFER_SIZE = 1 * 1024 * 1024; ///< 1 MB TLS heap central

// ============================================================================
// Thread stacks (BSS-resident, page-aligned)
// ============================================================================
// MITM server manager threads: 32 KB each. miniupnpc's upnpDiscover() +
// minissdpc helpers each allocate ~2 KB of stack frames; cumulative with
// IPC dispatch + CreateNetwork → NatPunch → Discover, 16 KB overflowed
// and DABRT'd. Two threads (TotalThreads = 2) share this stack size.
inline constexpr size_t MITM_THREAD_STACK_SIZE = 0x8000;        ///< 32 KB per MITM thread
inline constexpr size_t MITM_NUM_THREADS = 2;                   ///< Total MITM request threads
inline constexpr size_t MITM_NUM_EXTRA_THREADS = MITM_NUM_THREADS - 1;

// Config IPC service thread (ryu:cfg): 8 KB stack, light IPC dispatch.
inline constexpr size_t CFG_THREAD_STACK_SIZE = 0x2000;         ///< 8 KB config service thread

// Log maintenance thread: 4 KB stack, sleeps 2 s between idle checks.
inline constexpr size_t LOG_THREAD_STACK_SIZE = 0x1000;         ///< 4 KB log maintenance thread

// LDN receive thread (dedicated packet dispatch): 16 KB stack.
inline constexpr size_t LDN_RECV_THREAD_STACK_SIZE = 0x4000;    ///< 16 KB LDN receive thread

// LDN P2P connect worker (async ExternalProxy connect): 16 KB stack.
inline constexpr size_t LDN_P2P_CONNECT_THREAD_STACK_SIZE = 0x4000; ///< 16 KB P2P connect worker

// P2P accept thread (host side, listens for incoming peers): 16 KB stack.
inline constexpr size_t P2P_ACCEPT_THREAD_STACK_SIZE = 0x4000;  ///< 16 KB P2P accept thread

// P2P lease renewal thread (UPnP lease keepalive): 8 KB stack.
inline constexpr size_t P2P_LEASE_THREAD_STACK_SIZE = 0x2000;   ///< 8 KB P2P lease thread

// P2P client receive thread (joiner side, single client): 16 KB stack.
inline constexpr size_t P2P_CLIENT_RECV_THREAD_STACK_SIZE = 0x4000; ///< 16 KB P2P client recv

// P2P per-session receive thread pool (one per peer, max 8 players).
inline constexpr int    P2P_SESSION_STACK_COUNT = 8;            ///< One slot per max player
inline constexpr size_t P2P_SESSION_STACK_SIZE = 0x4000;        ///< 16 KB per session recv thread

// ============================================================================
// Socket / network buffers (libnx BSD config)
// ============================================================================
// These are passed into LibnxSocketInitConfig. Sizes are in bytes.
// ConcurrencyLimitMax = 14 is the highest bsd:s session count the kernel
// accepts; default of 3 saturated with P2P loopback sessions.
inline constexpr uint32_t TCP_TX_BUF_SIZE         = 0x800;   ///< 2 KB TCP tx initial
inline constexpr uint32_t TCP_RX_BUF_SIZE         = 0x1000;  ///< 4 KB TCP rx initial
inline constexpr uint32_t TCP_TX_BUF_MAX_SIZE     = 0x2000;  ///< 8 KB TCP tx max
inline constexpr uint32_t TCP_RX_BUF_MAX_SIZE     = 0x2000;  ///< 8 KB TCP rx max
inline constexpr uint32_t UDP_TX_BUF_SIZE         = 0x2000;  ///< 8 KB UDP tx
inline constexpr uint32_t UDP_RX_BUF_SIZE         = 0x2000;  ///< 8 KB UDP rx
inline constexpr uint32_t SOCKET_BUFFER_EFFICIENCY = 4;      ///< sb_efficiency (libnx)
inline constexpr uint32_t BSD_MAX_SESSIONS        = 14;      ///< ConcurrencyLimitMax
inline constexpr uint32_t BSD_INIT_VERSION        = 1;       ///< BSD init protocol version

// Per-call TCP recv staging buffer. 4 KB matches a typical MSS-sized TCP
// segment so one recv() usually returns a single packet.
inline constexpr size_t TCP_TEMP_RECV_BUFFER_SIZE = 4096;     ///< 4 KB per-call recv staging

// ============================================================================
// IPC server manager limits
// ============================================================================
inline constexpr size_t MITM_PORT_COUNT  = 2;    ///< ldn:u + bsd:u
inline constexpr size_t CFG_PORT_COUNT   = 1;    ///< ryu:cfg
inline constexpr size_t MITM_MAX_SESSIONS = 16;  ///< Max concurrent MITM sessions
inline constexpr size_t CFG_MAX_SESSIONS  = 2;   ///< Max concurrent ryu:cfg sessions
inline constexpr size_t MITM_POINTER_BUFFER_SIZE = 0x1000; ///< 4 KB MITM IPC pointer buffer
inline constexpr size_t MITM_MAX_DOMAINS          = 0x10;   ///< 16 MITM domains
inline constexpr size_t MITM_MAX_DOMAIN_OBJECTS   = 0x100;  ///< 256 MITM domain objects
inline constexpr size_t CFG_POINTER_BUFFER_SIZE   = 0x100;  ///< 256 B cfg IPC pointer buffer
inline constexpr size_t CFG_MAX_DOMAINS           = 0;      ///< cfg no domains
inline constexpr size_t CFG_MAX_DOMAIN_OBJECTS    = 0;      ///< cfg no domain objects

// ============================================================================
// Log subsystem buffers
// ============================================================================
inline constexpr size_t LOG_MESSAGE_MAX_LENGTH  = 256;  ///< Max single log message
inline constexpr size_t LOG_BUFFER_MAX_ENTRIES  = 64;   ///< Circular buffer entries for overlay
inline constexpr uint64_t LOG_MAINTENANCE_INTERVAL_SEC = 2;      ///< Log idle-check sleep (s)
inline constexpr int32_t  LOG_THREAD_PRIORITY_OFFSET   = 5;      ///< Priority offset vs cfg thread

// ============================================================================
// LDN / P2P subsystem buffers
// ============================================================================
inline constexpr size_t LDN_PROXY_MAX_PACKET_DATA = 0x800;     ///< 2 KB max packet data
inline constexpr size_t LDN_PROXY_BUFFER_SIZE =
    LDN_PROXY_MAX_PACKET_DATA * 4 + 256;                       ///< ~8.5 KB proxy buffer
inline constexpr size_t LDN_PROXY_MAX_QUEUED_PACKETS = 16;     ///< Pending packets per proxy
inline constexpr size_t MAX_PROXY_SOCKETS = 64;                ///< Max tracked proxy sockets
inline constexpr size_t MAX_PROXY_CONNECTIONS = 64;            ///< Max P2P proxy connections
inline constexpr size_t LDN_MAX_SCAN_RESULTS = 8;              ///< Max networks from scan
inline constexpr size_t MAX_NODES = 8;                         ///< Max LDN nodes (players)

// Protocol-level limits (kept here for memory budgeting; semantic
// definitions remain in protocol/types.hpp).
inline constexpr size_t MAX_PACKET_SIZE = 131072;              ///< 128 KB max protocol packet

} // namespace ryu_ldn::memory
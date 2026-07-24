/**
 * @file main.cpp
 * @brief ryu_ldn_nx - Nintendo Switch LDN to Ryujinx Server Bridge
 *
 * This sysmodule enables Nintendo Switch games to use the Ryujinx LDN
 * servers for online multiplayer, replacing the need for local wireless
 * or complex LAN play setups.
 *
 * Built on Atmosphere's libstratosphere framework.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include <stratosphere.hpp>

extern "C" {
#include <switch/services/bsd.h>
}

#include "ldn/ldn_mitm_service.hpp"
#include "bsd/bsd_mitm_service.hpp"
#include "config/config.hpp"
#include "config/config_ipc_service.hpp"
#include "config/game_whitelist.hpp"
#include "debug/log.hpp"
#include "ldn/ldn_shared_state.hpp"

namespace ams {

    namespace {

        // ====================================================================
        // Memory Configuration
        // ====================================================================

        // ====================================================================
        // Named constants for the libnx BSD socket configuration
        // ====================================================================
        // Previously magic numbers inlined in LibnxSocketInitConfig and
        // LibnxBsdInitConfig; extracted for readability. Values unchanged.

        /// @brief TCP transmit buffer initial size (bytes)
        constexpr u32 TCP_TX_BUF_SIZE = 0x800;
        /// @brief TCP receive buffer initial size (bytes)
        constexpr u32 TCP_RX_BUF_SIZE = 0x1000;
        /// @brief TCP transmit buffer max size (bytes)
        constexpr u32 TCP_TX_BUF_MAX_SIZE = 0x2000;
        /// @brief TCP receive buffer max size (bytes)
        constexpr u32 TCP_RX_BUF_MAX_SIZE = 0x2000;
        /// @brief UDP transmit buffer size (bytes)
        constexpr u32 UDP_TX_BUF_SIZE = 0x2000;
        /// @brief UDP receive buffer size (bytes)
        constexpr u32 UDP_RX_BUF_SIZE = 0x2000;
        /// @brief Socket buffer efficiency factor (sb_efficiency in libnx)
        constexpr u32 SOCKET_BUFFER_EFFICIENCY = 4;
        /// @brief Maximum number of concurrent BSD IPC sessions (ConcurrencyLimitMax)
        constexpr u32 BSD_MAX_SESSIONS = 14;
        /// @brief BSD init protocol version
        constexpr u32 BSD_INIT_VERSION = 1;
        /// @brief Alignment of the expanded heap backing buffer (bytes)
        constexpr size_t HEAP_ALIGNMENT = 0x40;
        /// @brief Number of MITM ports handled by ServerManager (ldn:u + bsd:u)
        constexpr size_t MITM_PORT_COUNT = 2;
        /// @brief Number of ryu:cfg config IPC ports
        constexpr size_t CFG_PORT_COUNT = 1;
        /// @brief Stack size for the log maintenance thread (bytes)
        constexpr size_t LOG_THREAD_STACK_SIZE = 0x1000;
        /// @brief Sleep interval of the log maintenance thread (seconds)
        constexpr uint64_t LOG_MAINTENANCE_INTERVAL_SEC = 2;
        /// @brief Priority offset of the log thread relative to the cfg thread
        constexpr s32 LOG_THREAD_PRIORITY_OFFSET = 5;

        /// Main malloc buffer size
        /// NOTE: Switch sysmodules share ~10MB total, keep this small!
        /// 1 MB is sufficient for TlsHeapCentral and gameplay traffic
        constexpr size_t MallocBufferSize = 1_MB;
        alignas(os::MemoryPageSize) constinit u8 g_malloc_buffer[MallocBufferSize];

        /// Socket buffer configuration
        // codeql[cpp/unused-static-function] — consteval, used at compile-time below
        consteval size_t GetLibnxBsdTransferMemorySize(const ::SocketInitConfig* config) {
            const u32 tcp_tx_buf_max_size = config->tcp_tx_buf_max_size != 0
                ? config->tcp_tx_buf_max_size : config->tcp_tx_buf_size;
            const u32 tcp_rx_buf_max_size = config->tcp_rx_buf_max_size != 0
                ? config->tcp_rx_buf_max_size : config->tcp_rx_buf_size;
            const u32 sum = tcp_tx_buf_max_size + tcp_rx_buf_max_size +
                            config->udp_tx_buf_size + config->udp_rx_buf_size;

            return static_cast<size_t>(config->sb_efficiency) * util::AlignUp(sum, os::MemoryPageSize);
        }

        /// Socket initialization configuration
        constexpr const ::SocketInitConfig LibnxSocketInitConfig = {
            .tcp_tx_buf_size     = TCP_TX_BUF_SIZE,
            .tcp_rx_buf_size     = TCP_RX_BUF_SIZE,
            .tcp_tx_buf_max_size = TCP_TX_BUF_MAX_SIZE,
            .tcp_rx_buf_max_size = TCP_RX_BUF_MAX_SIZE,
            .udp_tx_buf_size     = UDP_TX_BUF_SIZE,
            .udp_rx_buf_size     = UDP_RX_BUF_SIZE,
            .sb_efficiency       = SOCKET_BUFFER_EFFICIENCY,
            // num_bsd_sessions = number of IPC sessions to bsd:s — i.e. the
            // max concurrency for blocking BSD calls. Each blocked recv()/
            // accept()/send() holds one session for the entire IPC round-trip.
            //
            // Worst-case host-side concurrency in P2P mode (LDN = 8 players
            // total = host + 7 joiners):
            //   - master TCP recv          : 1
            //   - P2pProxyServer accept    : 1
            //   - P2pProxyClient (loopback) recv : 1
            //   - 8 P2pProxySession recv   (1 loopback + 7 joiners)
            //   = 11 blocking calls + marge for setsockopt/bind/send.
            //
            // The default of 3 saturated as soon as the loopback session was
            // alive (master recv + accept + 2 loopback recvs = 4 already > 3),
            // and bsd:s on Switch returns errno=113 (EHOSTUNREACH-mapped
            // "no resources") on the 4th concurrent call instead of blocking
            // — which is what made the AcceptLoop spin endlessly.
            //
            // 14 is ConcurrencyLimitMax in libstratosphere
            // (socket_constants.hpp:35), the highest value the kernel will
            // accept; transfer-memory size is independent of this count, so
            // headroom is free.
            .num_bsd_sessions    = BSD_MAX_SESSIONS,
            // bsd:s (System) is required for privileged socket options like
            // IP_MULTICAST_TTL / IP_MULTICAST_IF / IP_ADD_MEMBERSHIP that
            // miniupnpc's upnpDiscover() relies on. bsd:u returned EPERM and
            // miniupnpc crashed instead of handling the error gracefully.
            .bsd_service_type    = BsdServiceType_System,
        };

        /// Socket transfer memory buffer
        alignas(os::MemoryPageSize) constinit u8 g_socket_tmem_buffer[
            GetLibnxBsdTransferMemorySize(std::addressof(LibnxSocketInitConfig))];

        /// BSD initialization configuration
        constexpr const ::BsdInitConfig LibnxBsdInitConfig = {
            .version             = BSD_INIT_VERSION,
            .tmem_buffer         = g_socket_tmem_buffer,
            .tmem_buffer_size    = sizeof(g_socket_tmem_buffer),
            .tcp_tx_buf_size     = LibnxSocketInitConfig.tcp_tx_buf_size,
            .tcp_rx_buf_size     = LibnxSocketInitConfig.tcp_rx_buf_size,
            .tcp_tx_buf_max_size = LibnxSocketInitConfig.tcp_tx_buf_max_size,
            .tcp_rx_buf_max_size = LibnxSocketInitConfig.tcp_rx_buf_max_size,
            .udp_tx_buf_size     = LibnxSocketInitConfig.udp_tx_buf_size,
            .udp_rx_buf_size     = LibnxSocketInitConfig.udp_rx_buf_size,
            .sb_efficiency       = LibnxSocketInitConfig.sb_efficiency,
        };

    }

    // ========================================================================
    // MITM Server Configuration
    // ========================================================================

    namespace mitm {

        /// Thread priority for the MITM service
        const s32 ThreadPriority = 6;

        /// Total number of threads for request processing
        const size_t TotalThreads = 2;
        const size_t NumExtraThreads = TotalThreads - 1;

        /// Thread stack size
        // 32 KB: needed because miniupnpc's upnpDiscover() and minissdpc helpers
        // each allocate ~2 KB of stack frames; cumulative with IPC dispatch +
        // ICommunicationService::CreateNetwork → NatPunch → Discover, 16 KB
        // overflowed and DABRT'd on the very first call into upnpDiscover.
        const size_t ThreadStackSize = 0x8000;

        /// Thread stack
        alignas(os::MemoryPageSize) u8 g_thread_stack[ThreadStackSize];
        os::ThreadType g_thread;

        // Heap for dynamic allocations
        // NOTE: 384KB covers: game whitelist (~40KB), proxy socket receive queues
        // (up to ~45KB under lobby traffic with 900+ byte packets), pending-packet
        // buffer, ExpHeap overhead/fragmentation, and transient std::vector/std::deque
        // allocations. 96KB saturated under real gameplay traffic and caused DABRT
        // 0x101 on allocation failure.
        alignas(HEAP_ALIGNMENT) constinit u8 g_heap_memory[384_KB];
        constinit lmem::HeapHandle g_heap_handle;
        constinit bool g_heap_initialized;
        // Heap init mutex. `os::SdkMutex` provides a constexpr constructor that
        // constant-initializes the underlying critical section on Horizon (the
        // `AMS_OS_INTERNAL_CRITICAL_SECTION_IMPL_CONSTANT_INITIALIZER` path), so
        // `constinit` alone yields a usable lock. We still re-initialize it
        // explicitly in `InitializeSystemModule()` to make the dependency on
        // lock availability audible and to harden against non-Horizon toolchain
        // paths (pthread/windows) where the constexpr initializer may not fully
        // seed the critical section. `InitializeSdkMutex` is idempotent on an
        // already-initialized mutex — it just re-seeds the storage.
        constinit os::SdkMutex g_heap_init_mutex;

        lmem::HeapHandle GetHeapHandle() {
            if (AMS_UNLIKELY(!g_heap_initialized)) {
                std::scoped_lock lk(g_heap_init_mutex);

                if (AMS_LIKELY(!g_heap_initialized)) {
                    g_heap_handle = lmem::CreateExpHeap(g_heap_memory, sizeof(g_heap_memory),
                                                        lmem::CreateOption_ThreadSafe);
                    g_heap_initialized = true;
                }
            }

            return g_heap_handle;
        }

        void* Allocate(size_t size) {
            return lmem::AllocateFromExpHeap(GetHeapHandle(), size);
        }

        void Deallocate(void* p, size_t size) {
            AMS_UNUSED(size);
            return lmem::FreeToExpHeap(GetHeapHandle(), p);
        }

        namespace {

            /// Server manager options
            struct LdnMitmManagerOptions {
                static constexpr size_t PointerBufferSize   = 0x1000;
                static constexpr size_t MaxDomains          = 0x10;
                static constexpr size_t MaxDomainObjects    = 0x100;
                static constexpr bool   CanDeferInvokeRequest = false;
                static constexpr bool   CanManageMitmServers  = true;
            };

            /// Maximum concurrent sessions
            /// Higher value needed when intercepting all applications for BSD MITM
            constexpr size_t MaxSessions = 16;

            /// Port indices for MITM services
            constexpr int PortIndex_LdnMitm = 0;
            constexpr int PortIndex_BsdMitm = 1;

            /// Custom server manager for MITM (ldn:u + bsd:u)
            class ServerManager final : public sf::hipc::ServerManager<MITM_PORT_COUNT, LdnMitmManagerOptions, MaxSessions> {
            private:
                virtual ams::Result OnNeedsToAccept(int port_index, Server* server) override;
            };

            /**
             * @brief HIPC server manager driving the ldn:u and bsd:u MITM services.
             *
             * @role Owns the two MITM ports (PortIndex_LdnMitm, PortIndex_BsdMitm) and dispatches
             *       incoming IPC sessions to the corresponding service implementations via
             *       `OnNeedsToAccept`. Pumped by `LoopProcess()` from the main server thread
             *       (and the extra worker threads), so all acceptance is single-threaded per
             *       server manager instance.
             * @modified_by `mitm::InitializeSystemModule()` registers the MITM ports once at boot;
             *              `ServerManager::OnNeedsToAccept` accepts sessions at runtime. No external
             *              code mutates this object after initialization.
             * @thread_safety Atmosphere's `sf::hipc::ServerManager` is not internally synchronized;
             *                it is driven by `LoopProcess()` which must be called from a single
             *                thread per instance. Here it is pumped from the main thread plus the
             *                `NumExtraThreads` worker threads, all calling `LoopProcess()` on the
             *                same object — the manager's internal queue serializes dispatch safely
             *                across those threads. No other concurrent access occurs.
             */
            ServerManager g_server_manager;

            /**
             * @brief Monotonic counter assigning a unique ID to each accepted ldn:u MITM session.
             *
             * @role Generates the per-session identifier surfaced in `LOG_INFO` traces so concurrent
             *       LDN sessions can be told apart in logs.
             * @modified_by `ServerManager::OnNeedsToAccept` (this file, PortIndex_LdnMitm branch),
             *              via `++g_ldn_session_counter` when a new ldn:u session is accepted.
             * @thread_safety Not protected by any mutex. Acceptance is single-threaded by Atmosphere's
             *               server manager dispatch loop, so the increment is implicitly serialized.
             *               Reads from other threads would be racy; no such reads exist today.
             */
            static u32 g_ldn_session_counter = 0;

            /**
             * @brief Monotonic counter assigning a unique ID to each accepted bsd:u MITM session.
             *
             * @role Same purpose as g_ldn_session_counter but for the bsd:u MITM, so BSD sessions
             *       can be correlated in logs.
             * @modified_by `ServerManager::OnNeedsToAccept` (this file, PortIndex_BsdMitm branch),
             *              via `++g_bsd_session_counter` when a new bsd:u session is accepted.
             * @thread_safety Not protected by any mutex. Same single-threaded-accept rationale as
             *               g_ldn_session_counter; only `OnNeedsToAccept` mutates it.
             */
            static u32 g_bsd_session_counter = 0;

            Result ServerManager::OnNeedsToAccept(int port_index, Server* server) {
                LOG_INFO("OnNeedsToAccept: port_index=%d, server=%p", port_index, server);

                // Acknowledge the MITM session
                std::shared_ptr<::Service> fsrv;
                sm::MitmProcessInfo client_info;
                server->AcknowledgeMitmSession(std::addressof(fsrv), std::addressof(client_info));

                LOG_INFO("OnNeedsToAccept: Acknowledged session for pid=%lu, program_id=0x%016lx, fsrv=%p (handle=0x%x)",
                         client_info.process_id.value, client_info.program_id.value,
                         fsrv.get(), fsrv ? fsrv->session : 0);

                Result rc;
                switch (port_index) {
                    case PortIndex_LdnMitm:
                        {
                            u32 session_id = ++g_ldn_session_counter;
                            LOG_INFO("LDN MITM: Creating session #%u for pid=%lu", session_id, client_info.process_id.value);
                            // LDN MITM service (ldn:u)
                            rc = this->AcceptMitmImpl(
                                server,
                                sf::CreateSharedObjectEmplaced<
                                    mitm::ldn::ILdnMitMService,
                                    mitm::ldn::LdnMitMService>(decltype(fsrv)(fsrv), client_info),
                                fsrv);
                            LOG_INFO("LDN AcceptMitmImpl result: 0x%x (session #%u)", rc.GetValue(), session_id);
                        }
                        return rc;

                    case PortIndex_BsdMitm:
                        {
                            u32 session_id = ++g_bsd_session_counter;
                            LOG_INFO("BSD MITM: Creating session #%u for pid=%lu", session_id, client_info.process_id.value);
                            // BSD MITM service (bsd:u)
                            rc = this->AcceptMitmImpl(
                                server,
                                sf::CreateSharedObjectEmplaced<
                                    mitm::bsd::IBsdMitmService,
                                    mitm::bsd::BsdMitmService>(decltype(fsrv)(fsrv), client_info),
                                fsrv);
                            LOG_INFO("BSD AcceptMitmImpl result: 0x%x (session #%u)", rc.GetValue(), session_id);
                        }
                        return rc;

                    default:
                        AMS_ABORT("Unknown port index");
                }
            }

            // Extra threads for parallel request handling
            alignas(os::MemoryPageSize) u8 g_extra_thread_stacks[NumExtraThreads][ThreadStackSize];
            os::ThreadType g_extra_threads[NumExtraThreads];

            void LoopServerThread(void*) {
                g_server_manager.LoopProcess();
            }

            void ProcessForServerOnAllThreads(void*) {
                // Initialize extra threads
                if constexpr (NumExtraThreads > 0) {
                    const s32 priority = os::GetThreadCurrentPriority(os::GetCurrentThread());
                    for (size_t i = 0; i < NumExtraThreads; i++) {
                        R_ABORT_UNLESS(os::CreateThread(g_extra_threads + i, LoopServerThread,
                                                         nullptr, g_extra_thread_stacks[i],
                                                         ThreadStackSize, priority));
                        os::SetThreadNamePointer(g_extra_threads + i, "ryu_ldn::Thread");
                    }
                }

                // Start extra threads
                if constexpr (NumExtraThreads > 0) {
                    for (size_t i = 0; i < NumExtraThreads; i++) {
                        os::StartThread(g_extra_threads + i);
                    }
                }

                // Loop this thread
                LoopServerThread(nullptr);

                // Wait for extra threads to finish
                if constexpr (NumExtraThreads > 0) {
                    for (size_t i = 0; i < NumExtraThreads; i++) {
                        os::WaitThread(g_extra_threads + i);
                    }
                }
            }

        }

    }

    // ========================================================================
    // Configuration IPC Service (ryu:cfg)
    // ========================================================================

    namespace cfg {

        /// Thread priority for config service
        const s32 ThreadPriority = 10;

        /// Thread stack size
        const size_t ThreadStackSize = 0x2000;

        /// Thread stack
        alignas(os::MemoryPageSize) u8 g_thread_stack[ThreadStackSize];
        os::ThreadType g_thread;

        /// Server manager options for config service
        struct ConfigServerManagerOptions {
            static constexpr size_t PointerBufferSize   = 0x100;
            static constexpr size_t MaxDomains          = 0;
            static constexpr size_t MaxDomainObjects    = 0;
            static constexpr bool   CanDeferInvokeRequest = false;
            static constexpr bool   CanManageMitmServers  = false;
        };

        /// Maximum concurrent sessions for config service
        constexpr size_t MaxSessions = 2;

        /// Server manager for ryu:cfg service
        using ConfigServerManager = sf::hipc::ServerManager<CFG_PORT_COUNT, ConfigServerManagerOptions, MaxSessions>;
        /**
         * @brief HIPC server manager driving the custom `ryu:cfg` IPC service.
         *
         * @role Owns the single config service port that the Tesla overlay talks to for live
         *       configuration changes and LDN status display. Pumped by `LoopProcess()` from the
         *       dedicated config thread started in `InitializeSystemModule()`.
         * @modified_by `cfg::InitializeConfig()` registers the `ConfigService` object once at boot;
         *              no further mutation occurs after registration. Runtime traffic is read-only
         *              with respect to the manager itself.
         * @thread_safety Same caveat as `mitm::g_server_manager`: `sf::hipc::ServerManager` is not
         *                internally synchronized and must be pumped from a single thread. Here it
         *                is driven only by the config thread via `LoopConfigServerThread`, so the
         *                implicit single-pumper contract is satisfied.
         */
        ConfigServerManager g_config_server_manager;

        /// Config service thread entry point
        void LoopConfigServerThread(void*) {
            g_config_server_manager.LoopProcess();
        }

        /// Log maintenance thread stack
        alignas(os::MemoryPageSize) u8 g_log_thread_stack[LOG_THREAD_STACK_SIZE];
        os::ThreadType g_log_thread;

        /// Log maintenance thread entry point (checks file idle timeout)
        void LoopLogMaintenanceThread(void*) {
            while (true) {
                // Sleep for 2 seconds
                svc::SleepThread(TimeSpan::FromSeconds(LOG_MAINTENANCE_INTERVAL_SEC).GetNanoSeconds());

                // Check if log file should be closed due to idle timeout
                ryu_ldn::debug::g_logger.check_idle_timeout();
            }
        }

    }

    // ========================================================================
    // System Module Initialization
    // ========================================================================


    // ========================================================================
    namespace init {

        void InitializeSystemModule() {
            // Initialize service manager connection
            R_ABORT_UNLESS(sm::Initialize());

            // Host-test safety net: re-seeds the mutex storage on non-Horizon
            // platforms. On Horizon (`__SWITCH__`), `constinit` already
            // constant-initializes the underlying critical section, so the
            // explicit `InitializeSdkMutex` call is redundant and is skipped
            // to avoid a double-init on the SdkMutex. The host test build
            // (g++ with -DTEST_BUILD) has no `constinit` zero-init guarantee
            // for the mutex internals, so we re-seed there.
#ifndef __SWITCH__
            os::InitializeSdkMutex(std::addressof(g_heap_init_mutex));
#endif

            // Initialize filesystem
            fs::InitializeForSystem();
            fs::SetAllocator(mitm::Allocate, mitm::Deallocate);
            fs::SetEnabledAutoAbort(false);

            // Mount SD card for configuration
            R_ABORT_UNLESS(fs::MountSdCard("sdmc"));

            // Ensure config file exists (create with defaults if not)
            ryu_ldn::config::ensure_config_exists(ryu_ldn::config::CONFIG_PATH);

            // Load configuration
            ryu_ldn::config::Config config = ryu_ldn::config::get_default_config();
            ryu_ldn::config::load_config(ryu_ldn::config::CONFIG_PATH, config);

            // Initialize logger with debug settings
            ryu_ldn::debug::g_logger.init(config.debug, ryu_ldn::config::LOG_PATH);
            LOG_INFO("ryu_ldn_nx sysmodule starting");
            LOG_INFO("Config loaded from %s", ryu_ldn::config::CONFIG_PATH);

            // Load game whitelist from file (once at startup)
            ryu_ldn::config::LoadWhitelist();
            LOG_VERBOSE("Server: %s:%u", config.server.host, config.server.port);

            // Initialize network services
            R_ABORT_UNLESS(nifmInitialize(NifmServiceType_Admin));
            R_ABORT_UNLESS(bsdInitialize(&LibnxBsdInitConfig,
                                          LibnxSocketInitConfig.num_bsd_sessions,
                                          LibnxSocketInitConfig.bsd_service_type));
            R_ABORT_UNLESS(socketInitialize(&LibnxSocketInitConfig));

        }

        void FinalizeSystemModule() {
            LOG_INFO("ryu_ldn_nx sysmodule shutting down");

            // Flush logs first, then tear down in reverse init order:
            // socket → bsd → nifm. The socket layer must exit before BSD
            // because libnx socket cleanup closes file descriptors that the
            // BSD service still tracks.
            ryu_ldn::debug::g_logger.flush();
            socketExit();
            bsdExit();
            nifmExit();
            fs::Unmount("sdmc");
        }

        void Startup() {
            // Initialize the global malloc allocator
            init::InitializeAllocator(g_malloc_buffer, sizeof(g_malloc_buffer));
        }

    }

    // ========================================================================
    // Exit Handler (should never be called)
    // ========================================================================

    void NORETURN Exit(int rc) {
        AMS_UNUSED(rc);
        AMS_ABORT("Exit called by immortal process");
    }

    // ========================================================================
    // Main Entry Point
    // ========================================================================

    void Main() {
        // Initialize global configuration for IPC service
        ryu_ldn::ipc::InitializeConfig();

        // ====================================================================
        // Register ryu:cfg configuration service
        // ====================================================================
        LOG_INFO("Registering ryu:cfg config service");
        constexpr sm::ServiceName ConfigServiceName = sm::ServiceName::Encode("ryu:cfg");

        // Create the config service object and register it
        auto config_service = sf::CreateSharedObjectEmplaced<
            ryu_ldn::ipc::IConfigService,
            ryu_ldn::ipc::ConfigService>();

        R_ABORT_UNLESS(cfg::g_config_server_manager.RegisterObjectForServer(
            std::move(config_service), ConfigServiceName, cfg::MaxSessions));
        LOG_INFO("Config service ryu:cfg registered successfully");

        // Create config service thread
        R_ABORT_UNLESS(os::CreateThread(
            &cfg::g_thread,
            cfg::LoopConfigServerThread,
            nullptr,
            cfg::g_thread_stack,
            cfg::ThreadStackSize,
            cfg::ThreadPriority));

        os::SetThreadNamePointer(&cfg::g_thread, "ryu_ldn::CfgThread");
        os::StartThread(&cfg::g_thread);

        // Create log maintenance thread (for idle timeout)
        R_ABORT_UNLESS(os::CreateThread(
            &cfg::g_log_thread,
            cfg::LoopLogMaintenanceThread,
            nullptr,
            cfg::g_log_thread_stack,
            sizeof(cfg::g_log_thread_stack),
             cfg::ThreadPriority + LOG_THREAD_PRIORITY_OFFSET));  // Lower priority than config service

        os::SetThreadNamePointer(&cfg::g_log_thread, "ryu_ldn::LogThread");
        os::StartThread(&cfg::g_log_thread);

        // ====================================================================
        // Register MITM services
        // ====================================================================

        // Register ldn:u MITM service (port 0)
        LOG_INFO("Registering ldn:u MITM service");
        constexpr sm::ServiceName LdnMitmServiceName = sm::ServiceName::Encode("ldn:u");
        R_ABORT_UNLESS((mitm::g_server_manager.RegisterMitmServer<
            mitm::ldn::LdnMitMService>(mitm::PortIndex_LdnMitm, LdnMitmServiceName)));
        LOG_INFO("ldn:u MITM service registered successfully");

        // Register bsd:u MITM service (port 1)
        // This allows us to intercept game sockets that target LDN addresses (10.114.x.x)
        LOG_INFO("Registering bsd:u MITM service");
        constexpr sm::ServiceName BsdMitmServiceName = sm::ServiceName::Encode("bsd:u");
        R_ABORT_UNLESS((mitm::g_server_manager.RegisterMitmServer<
            mitm::bsd::BsdMitmService>(mitm::PortIndex_BsdMitm, BsdMitmServiceName)));
        LOG_INFO("bsd:u MITM service registered successfully");



        // Create MITM processing thread
        R_ABORT_UNLESS(os::CreateThread(
            &mitm::g_thread,
            mitm::ProcessForServerOnAllThreads,
            nullptr,
            mitm::g_thread_stack,
            mitm::ThreadStackSize,
            mitm::ThreadPriority));

        os::SetThreadNamePointer(&mitm::g_thread, "ryu_ldn::MainThread");
        os::StartThread(&mitm::g_thread);

        // Wait for MITM thread (runs forever)
        // Note: Config thread also runs forever in parallel
        os::WaitThread(&mitm::g_thread);
    }

}

// ============================================================================
// Custom Memory Allocators
// ============================================================================

void* operator new(size_t size) {
    return ams::mitm::Allocate(size);
}

void* operator new(size_t size, const std::nothrow_t&) {
    return ams::mitm::Allocate(size);
}

void operator delete(void* p) {
    return ams::mitm::Deallocate(p, 0);
}

void operator delete(void* p, size_t size) {
    return ams::mitm::Deallocate(p, size);
}

void* operator new[](size_t size) {
    return ams::mitm::Allocate(size);
}

void* operator new[](size_t size, const std::nothrow_t&) {
    return ams::mitm::Allocate(size);
}

void operator delete[](void* p) {
    return ams::mitm::Deallocate(p, 0);
}

void operator delete[](void* p, size_t size) {
    return ams::mitm::Deallocate(p, size);
}

// ============================================================================
// C++17 aligned allocation overloads
// ============================================================================
// The custom expanded heap (lmem::ExpHeap) has a fixed alignment that is
// already sufficient for all standard types. These overloads delegate to the
// existing non-aligned versions, ignoring the requested alignment. This
// satisfies the C++17 standard library which calls these overloads when
// allocating over-aligned types (e.g., alignas(64) buffers).
//
// Without these overloads, the linker fails to resolve the aligned new/delete
// symbols on toolchains that define them (devkitPro/gcc 8.1+), causing a
// build error whenever std::aligned_alloc or an over-aligned type is used.

void* operator new(size_t size, std::align_val_t /*alignment*/) {
    return ams::mitm::Allocate(size);
}

void* operator new(size_t size, std::align_val_t /*alignment*/, const std::nothrow_t&) {
    return ams::mitm::Allocate(size);
}

void operator delete(void* p, std::align_val_t /*alignment*/) {
    return ams::mitm::Deallocate(p, 0);
}

void operator delete(void* p, size_t size, std::align_val_t /*alignment*/) {
    return ams::mitm::Deallocate(p, size);
}

void* operator new[](size_t size, std::align_val_t /*alignment*/) {
    return ams::mitm::Allocate(size);
}

void* operator new[](size_t size, std::align_val_t /*alignment*/, const std::nothrow_t&) {
    return ams::mitm::Allocate(size);
}

void operator delete[](void* p, std::align_val_t /*alignment*/) {
    return ams::mitm::Deallocate(p, 0);
}

void operator delete[](void* p, size_t size, std::align_val_t /*alignment*/) {
    return ams::mitm::Deallocate(p, size);
}

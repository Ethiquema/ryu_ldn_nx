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
#include "config/config.hpp"

namespace ams {

    namespace {

        // ====================================================================
        // Memory Configuration
        // ====================================================================

        /// Main malloc buffer size
        constexpr size_t MallocBufferSize = 1_MB;
        alignas(os::MemoryPageSize) constinit u8 g_malloc_buffer[MallocBufferSize];

        /// Socket buffer configuration
        consteval size_t GetLibnxBsdTransferMemorySize(const ::SocketInitConfig* config) {
            const u32 tcp_tx_buf_max_size = config->tcp_tx_buf_max_size != 0
                ? config->tcp_tx_buf_max_size : config->tcp_tx_buf_size;
            const u32 tcp_rx_buf_max_size = config->tcp_rx_buf_max_size != 0
                ? config->tcp_rx_buf_max_size : config->tcp_rx_buf_size;
            const u32 sum = tcp_tx_buf_max_size + tcp_rx_buf_max_size +
                            config->udp_tx_buf_size + config->udp_rx_buf_size;

            return config->sb_efficiency * util::AlignUp(sum, os::MemoryPageSize);
        }

        /// Socket initialization configuration
        constexpr const ::SocketInitConfig LibnxSocketInitConfig = {
            .tcp_tx_buf_size     = 0x800,
            .tcp_rx_buf_size     = 0x1000,
            .tcp_tx_buf_max_size = 0x2000,
            .tcp_rx_buf_max_size = 0x2000,
            .udp_tx_buf_size     = 0x2000,
            .udp_rx_buf_size     = 0x2000,
            .sb_efficiency       = 4,
            .num_bsd_sessions    = 3,
            .bsd_service_type    = BsdServiceType_User,
        };

        /// Socket transfer memory buffer
        alignas(os::MemoryPageSize) constinit u8 g_socket_tmem_buffer[
            GetLibnxBsdTransferMemorySize(std::addressof(LibnxSocketInitConfig))];

        /// BSD initialization configuration
        constexpr const ::BsdInitConfig LibnxBsdInitConfig = {
            .version             = 1,
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
        const size_t ThreadStackSize = 0x4000;

        /// Thread stack
        alignas(os::MemoryPageSize) u8 g_thread_stack[ThreadStackSize];
        os::ThreadType g_thread;

        // Heap for dynamic allocations
        alignas(0x40) constinit u8 g_heap_memory[128_KB];
        constinit lmem::HeapHandle g_heap_handle;
        constinit bool g_heap_initialized;
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
            constexpr size_t MaxSessions = 3;

            /// Custom server manager for MITM
            class ServerManager final : public sf::hipc::ServerManager<1, LdnMitmManagerOptions, MaxSessions> {
            private:
                virtual ams::Result OnNeedsToAccept(int port_index, Server* server) override;
            };

            ServerManager g_server_manager;

            Result ServerManager::OnNeedsToAccept(int port_index, Server* server) {
                AMS_UNUSED(port_index);

                // Acknowledge the MITM session
                std::shared_ptr<::Service> fsrv;
                sm::MitmProcessInfo client_info;
                server->AcknowledgeMitmSession(std::addressof(fsrv), std::addressof(client_info));

                // Create and accept our MITM service
                return this->AcceptMitmImpl(
                    server,
                    sf::CreateSharedObjectEmplaced<
                        mitm::ldn::ILdnMitMService,
                        mitm::ldn::LdnMitMService>(decltype(fsrv)(fsrv), client_info),
                    fsrv);
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
    // System Module Initialization
    // ========================================================================

    namespace init {

        void InitializeSystemModule() {
            // Initialize service manager connection
            R_ABORT_UNLESS(sm::Initialize());

            // Initialize filesystem
            fs::InitializeForSystem();
            fs::SetAllocator(mitm::Allocate, mitm::Deallocate);
            fs::SetEnabledAutoAbort(false);

            // Mount SD card for configuration
            R_ABORT_UNLESS(fs::MountSdCard("sdmc"));

            // Ensure config file exists (create with defaults if not)
            ryu_ldn::config::ensure_config_exists(ryu_ldn::config::CONFIG_PATH);

            // Initialize network services
            R_ABORT_UNLESS(nifmInitialize(NifmServiceType_Admin));
            R_ABORT_UNLESS(bsdInitialize(&LibnxBsdInitConfig,
                                          LibnxSocketInitConfig.num_bsd_sessions,
                                          LibnxSocketInitConfig.bsd_service_type));
            R_ABORT_UNLESS(socketInitialize(&LibnxSocketInitConfig));
        }

        void FinalizeSystemModule() {
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
        // Register ldn:u MITM service
        constexpr sm::ServiceName MitmServiceName = sm::ServiceName::Encode("ldn:u");
        R_ABORT_UNLESS((mitm::g_server_manager.RegisterMitmServer<
            mitm::ldn::LdnMitMService>(0, MitmServiceName)));

        // Create main processing thread
        R_ABORT_UNLESS(os::CreateThread(
            &mitm::g_thread,
            mitm::ProcessForServerOnAllThreads,
            nullptr,
            mitm::g_thread_stack,
            mitm::ThreadStackSize,
            mitm::ThreadPriority));

        os::SetThreadNamePointer(&mitm::g_thread, "ryu_ldn::MainThread");
        os::StartThread(&mitm::g_thread);

        // Wait for main thread (runs forever)
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

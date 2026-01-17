# ==========================================
# Memory Trace - Leak Detection
# ==========================================
# Track potential memory leaks

echo [MEMORY] Loading leak detection...\n

# Component destructors - ensure cleanup
break ryu_ldn::network::RyuLdnClient::~RyuLdnClient
commands
    silent
    echo [MEMORY:LEAK] RyuLdnClient destructor - checking cleanup\n
    backtrace 2
    continue
end

break ams::mitm::bsd::BsdMitmService::~BsdMitmService
commands
    silent
    echo [MEMORY:LEAK] BsdMitmService destructor - checking cleanup\n
    backtrace 2
    continue
end

break ams::mitm::p2p::P2pProxyServer::Stop
commands
    silent
    echo [MEMORY:LEAK] P2P Proxy stopping - checking resource cleanup\n
    backtrace 2
    continue
end

break ams::mitm::p2p::P2pProxyClient::Disconnect
commands
    silent
    echo [MEMORY:LEAK] P2P Client disconnecting - checking cleanup\n
    backtrace 2
    continue
end

# Session cleanup
break ams::mitm::p2p::P2pProxySession::Disconnect
commands
    silent
    echo [MEMORY:LEAK] P2P Session disconnecting - checking cleanup\n
    backtrace 2
    continue
end

# Logger cleanup
break ryu_ldn::debug::Logger::close_file
commands
    silent
    echo [MEMORY:LEAK] Logger closing - checking buffer cleanup\n
    continue
end

echo [MEMORY] Leak detection: 6 breakpoints\n

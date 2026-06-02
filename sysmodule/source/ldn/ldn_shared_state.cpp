/**
 * @file ldn_shared_state.cpp
 * @brief Implementation of SharedState singleton
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "ldn_shared_state.hpp"
#include "ldn_types.hpp"
#include "../debug/log.hpp"

namespace ams::mitm::ldn {

SharedState& SharedState::GetInstance() {
    static SharedState instance;
    return instance;
}

void SharedState::Reset() {
    std::scoped_lock lk(m_mutex);
    m_game_active = false;
    m_process_id = 0;
    m_ldn_state = CommState::None;
    m_node_count = 0;
    m_max_nodes = 0;
    m_local_node_id = 0;
    m_is_host = false;
    m_last_rtt_ms = 0;
    m_reconnect_requested = false;
}

// =============================================================================
// Game Active State
// =============================================================================

void SharedState::SetGameActive(bool active, u64 process_id) {
    std::scoped_lock lk(m_mutex);
    m_game_active = active;
    m_process_id = active ? process_id : 0;

    if (!active) {
        // Reset runtime state when game exits
        m_ldn_state = CommState::None;
        m_node_count = 0;
        m_max_nodes = 0;
        m_local_node_id = 0;
        m_is_host = false;
        m_last_rtt_ms = 0;
    }
}

bool SharedState::IsGameActive() const {
    std::scoped_lock lk(m_mutex);
    return m_game_active;
}

u64 SharedState::GetActiveProcessId() const {
    std::scoped_lock lk(m_mutex);
    return m_process_id;
}

void SharedState::SetLdnPid(u64 pid) {
    std::scoped_lock lk(m_mutex);
    u64 old_pid = m_ldn_pid;
    m_ldn_pid = pid;
    LOG_INFO("SharedState::SetLdnPid: %lu -> %lu (BSD sessions opened after this will be intercepted)",
             old_pid, pid);
}

u64 SharedState::GetLdnPid() const {
    std::scoped_lock lk(m_mutex);
    return m_ldn_pid;
}

bool SharedState::IsLdnPid(u64 pid) const {
    std::scoped_lock lk(m_mutex);
    return m_ldn_pid != 0 && m_ldn_pid == pid;
}

// =============================================================================
// LDN Game Whitelist (for BSD MITM)
// =============================================================================

void SharedState::LoadLdnWhitelist(const std::vector<u64>& game_ids) {
    std::scoped_lock lk(m_mutex);
    m_ldn_games.clear();
    for (u64 id : game_ids) {
        m_ldn_games.insert(id);
    }
    LOG_INFO("SharedState::LoadLdnWhitelist: Loaded %zu games", m_ldn_games.size());
}

bool SharedState::IsLdnGame(u64 program_id) const {
    std::scoped_lock lk(m_mutex);
    return m_ldn_games.find(program_id) != m_ldn_games.end();
}

size_t SharedState::GetWhitelistSize() const {
    std::scoped_lock lk(m_mutex);
    return m_ldn_games.size();
}

// =============================================================================
// LDN State
// =============================================================================

void SharedState::SetLdnState(CommState state) {
    std::scoped_lock lk(m_mutex);
    m_ldn_state = state;
}

CommState SharedState::GetLdnState() const {
    std::scoped_lock lk(m_mutex);
    return m_ldn_state;
}

// =============================================================================
// Session Info
// =============================================================================

void SharedState::SetSessionInfo(u8 node_count, u8 max_nodes, u8 local_node_id, bool is_host) {
    std::scoped_lock lk(m_mutex);
    m_node_count = node_count;
    m_max_nodes = max_nodes;
    m_local_node_id = local_node_id;
    m_is_host = is_host;
}

void SharedState::GetSessionInfo(u8& node_count, u8& max_nodes, u8& local_node_id, bool& is_host) const {
    std::scoped_lock lk(m_mutex);
    node_count = m_node_count;
    max_nodes = m_max_nodes;
    local_node_id = m_local_node_id;
    is_host = m_is_host;
}

SessionInfo SharedState::GetSessionInfoStruct() const {
    std::scoped_lock lk(m_mutex);
    SessionInfo info{};
    info.node_count = m_node_count;
    info.node_count_max = m_max_nodes;
    info.local_node_id = m_local_node_id;
    info.is_host = m_is_host ? 1 : 0;
    std::memset(info.reserved, 0, sizeof(info.reserved));
    return info;
}

// =============================================================================
// RTT
// =============================================================================

void SharedState::SetLastRtt(u32 rtt_ms) {
    std::scoped_lock lk(m_mutex);
    m_last_rtt_ms = rtt_ms;
}

u32 SharedState::GetLastRtt() const {
    std::scoped_lock lk(m_mutex);
    return m_last_rtt_ms;
}

// =============================================================================
// Reconnect Request
// =============================================================================

void SharedState::RequestReconnect() {
    std::scoped_lock lk(m_mutex);
    m_reconnect_requested = true;
}

bool SharedState::ConsumeReconnectRequest() {
    std::scoped_lock lk(m_mutex);
    bool was_requested = m_reconnect_requested;
    m_reconnect_requested = false;
    return was_requested;
}

// =============================================================================
// Active Session Counting (IPC session exit detection)
// =============================================================================

void SharedState::IncrementSessionCount() {
    u32 old_count = m_active_sessions.fetch_add(1, std::memory_order_relaxed);
    LOG_INFO("SharedState::IncrementSessionCount: %u -> %u", old_count, old_count + 1);
}

u32 SharedState::DecrementSessionCount() {
    u32 old_count = m_active_sessions.fetch_sub(1, std::memory_order_acq_rel);
    u32 new_count = old_count - 1;
    LOG_INFO("SharedState::DecrementSessionCount: %u -> %u", old_count, new_count);
    return new_count;
}

u32 SharedState::GetActiveSessionCount() const {
    return m_active_sessions.load(std::memory_order_acquire);
}

} // namespace ams::mitm::ldn

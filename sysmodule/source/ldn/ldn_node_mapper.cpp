/**
 * @file ldn_node_mapper.cpp
 * @brief Node ID to IP address mapping implementation
 *
 * This module implements the mapping between LDN node IDs and their
 * network addresses for proxy data routing. It supports both unicast
 * and broadcast routing decisions.
 *
 * ## Thread Safety
 * All public methods are thread-safe using os::SdkMutex.
 *
 * ## Node Lifecycle
 * 1. Nodes are added when SyncNetwork message is received
 * 2. Nodes are removed on disconnect or when network info updates
 * 3. Clear() resets all state (used on Finalize/CloseStation/CloseAP)
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "ldn_node_mapper.hpp"

namespace ams::mitm::ldn {

/**
 * @brief Constructor - initializes empty node map
 *
 * Sets up all node entries with default values:
 * - node_id: index (0-7)
 * - ipv4_address: 0
 * - is_connected: false
 */
LdnNodeMapper::LdnNodeMapper()
    : m_mutex()
    , m_nodes{}
    , m_local_node_id(0xFF)  // 0xFF = not assigned
{
    // Initialize empty node entries with their respective IDs
    for (size_t i = 0; i < MaxNodes; i++) {
        m_nodes[i].node_id = static_cast<u32>(i);
        m_nodes[i].ipv4_address = 0;
        m_nodes[i].is_connected = false;
    }
}

/**
 * @brief Add or update a node in the map
 *
 * Called when a new player joins the network or when network info
 * is synchronized. If the node already exists, its IP is updated.
 *
 * @param node_id Node ID (0-7), ignored if >= MaxNodes
 * @param ipv4 IPv4 address in network byte order
 */
void LdnNodeMapper::AddNode(u32 node_id, u32 ipv4) {
    // Validate node_id range
    if (node_id >= MaxNodes) {
        return;
    }

    std::scoped_lock lk(m_mutex);
    m_nodes[node_id].ipv4_address = ipv4;
    m_nodes[node_id].is_connected = true;
}

/**
 * @brief Remove a node from the map
 *
 * Called when a player disconnects from the network.
 * The entry is marked as disconnected but IP is preserved
 * for potential reconnection scenarios.
 *
 * @param node_id Node ID to remove, ignored if >= MaxNodes
 */
void LdnNodeMapper::RemoveNode(u32 node_id) {
    // Validate node_id range
    if (node_id >= MaxNodes) {
        return;
    }

    std::scoped_lock lk(m_mutex);
    m_nodes[node_id].is_connected = false;
}

/**
 * @brief Check if a node is connected
 *
 * Used before attempting to route data to a node.
 *
 * @param node_id Node ID to check
 * @return true if node exists and is connected, false otherwise
 */
bool LdnNodeMapper::IsNodeConnected(u32 node_id) const {
    // Invalid node IDs are never connected
    if (node_id >= MaxNodes) {
        return false;
    }

    std::scoped_lock lk(m_mutex);
    return m_nodes[node_id].is_connected;
}

/**
 * @brief Get node's IPv4 address
 *
 * Returns the IPv4 address for routing packets to this node.
 *
 * @param node_id Node ID
 * @return IPv4 address in network byte order, 0 if node not found
 */
u32 LdnNodeMapper::GetNodeIp(u32 node_id) const {
    // Invalid node IDs return 0
    if (node_id >= MaxNodes) {
        return 0;
    }

    std::scoped_lock lk(m_mutex);
    return m_nodes[node_id].ipv4_address;
}

/**
 * @brief Get number of connected nodes
 *
 * Counts all nodes marked as connected. Used for statistics
 * and to determine broadcast target count.
 *
 * @return Number of connected nodes (0-8)
 */
size_t LdnNodeMapper::GetConnectedCount() const {
    std::scoped_lock lk(m_mutex);

    size_t count = 0;
    for (size_t i = 0; i < MaxNodes; i++) {
        if (m_nodes[i].is_connected) {
            count++;
        }
    }
    return count;
}

/**
 * @brief Clear all nodes
 *
 * Resets the mapper to initial state. Called when:
 * - Finalize() is called
 * - CloseStation() or CloseAccessPoint() is called
 * - Network connection is lost
 */
void LdnNodeMapper::Clear() {
    std::scoped_lock lk(m_mutex);

    for (size_t i = 0; i < MaxNodes; i++) {
        m_nodes[i].ipv4_address = 0;
        m_nodes[i].is_connected = false;
    }
    m_local_node_id = 0xFF;  // Reset local node assignment
}

/**
 * @brief Update from NetworkInfo structure
 *
 * Synchronizes the node map with the NetworkInfo received from
 * the server. This replaces all existing node data.
 *
 * ## Mapping
 * - NetworkInfo.ldn.nodes[i].nodeId → m_nodes index
 * - NetworkInfo.ldn.nodes[i].ipv4Address → m_nodes[].ipv4_address
 * - NetworkInfo.ldn.nodes[i].isConnected → m_nodes[].is_connected
 *
 * @param info Network info containing node list
 */
void LdnNodeMapper::UpdateFromNetworkInfo(const NetworkInfo& info) {
    std::scoped_lock lk(m_mutex);

    // Clear existing nodes first
    for (size_t i = 0; i < MaxNodes; i++) {
        m_nodes[i].ipv4_address = 0;
        m_nodes[i].is_connected = false;
    }

    // Add nodes from network info
    // nodeCount indicates how many valid entries are in the array
    for (u8 i = 0; i < info.ldn.nodeCount && i < MaxNodes; i++) {
        const auto& node = info.ldn.nodes[i];

        // Only add nodes that are marked as connected
        if (node.isConnected) {
            m_nodes[node.nodeId].ipv4_address = node.ipv4Address;
            m_nodes[node.nodeId].is_connected = true;
        }
    }
}

/**
 * @brief Check if packet should be routed to a specific node
 *
 * Determines whether a proxy data packet should be forwarded to
 * a particular node based on the packet's destination and routing rules.
 *
 * ## Routing Rules
 *
 * ### Broadcast (dest_node_id == BroadcastNodeId)
 * - Route to ALL connected nodes EXCEPT the source node
 * - Prevents echo back to sender
 *
 * ### Unicast (specific dest_node_id)
 * - Route ONLY to the destination node
 * - Destination must be connected
 *
 * @param dest_node_id Destination from packet header (or BroadcastNodeId for broadcast)
 * @param source_node_id Source from packet header (used to avoid echo)
 * @param target_node_id The node we're considering routing to
 * @return true if packet should be sent to target_node_id
 */
bool LdnNodeMapper::ShouldRouteToNode(u32 dest_node_id, u32 source_node_id, u32 target_node_id) const {
    // Invalid target node IDs are never routed to
    if (target_node_id >= MaxNodes) {
        return false;
    }

    std::scoped_lock lk(m_mutex);

    // First check: target must be connected
    if (!m_nodes[target_node_id].is_connected) {
        return false;
    }

    // Broadcast routing: send to all connected except source
    if (dest_node_id == BroadcastNodeId) {
        // Don't echo back to source
        return target_node_id != source_node_id;
    }

    // Unicast routing: only send to destination
    return dest_node_id == target_node_id;
}

} // namespace ams::mitm::ldn
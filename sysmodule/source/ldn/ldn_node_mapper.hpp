/**
 * @file ldn_node_mapper.hpp
 * @brief Node ID to IP address mapping for LDN proxy
 *
 * Manages the mapping between LDN node IDs (0-7) and their corresponding
 * IPv4 addresses for data routing.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>
#include "ldn_types.hpp"

namespace ams::mitm::ldn {

/**
 * @brief Node mapper for LDN proxy data routing
 *
 * Maps node IDs to their network information for routing proxy data.
 * Thread-safe using SdkMutex.
 */
class LdnNodeMapper {
public:
    /// Maximum number of nodes in LDN network
    static constexpr size_t MaxNodes = 8;

    /// Broadcast destination node ID
    static constexpr u32 BroadcastNodeId = 0xFFFFFFFF;

    /**
     * @brief Node entry containing connection info
     */
    struct NodeEntry {
        u32 node_id;        ///< Node ID (0-7)
        u32 ipv4_address;   ///< IPv4 address (network byte order)
        bool is_connected;  ///< Connection status
    };

    /**
     * @brief Constructor - initializes empty node map
     */
    LdnNodeMapper();

    /**
     * @brief Add or update a node in the map
     *
     * @param node_id Node ID (0-7)
     * @param ipv4 IPv4 address
     */
    void AddNode(u32 node_id, u32 ipv4);

    /**
     * @brief Remove a node from the map
     *
     * @param node_id Node ID to remove
     */
    void RemoveNode(u32 node_id);

    /**
     * @brief Check if a node is connected
     *
     * @param node_id Node ID to check
     * @return true if connected
     */
    bool IsNodeConnected(u32 node_id) const;

    /**
     * @brief Get node's IPv4 address
     *
     * @param node_id Node ID
     * @return IPv4 address (0 if not found)
     */
    u32 GetNodeIp(u32 node_id) const;

    /**
     * @brief Get number of connected nodes
     *
     * @return Connected node count
     */
    size_t GetConnectedCount() const;

    /**
     * @brief Clear all nodes
     */
    void Clear();

    /**
     * @brief Update from NetworkInfo structure
     *
     * @param info Network info containing node list
     */
    void UpdateFromNetworkInfo(const NetworkInfo& info);

    /**
     * @brief Check if packet should be routed to a specific node
     *
     * @param dest_node_id Destination node ID from packet (or BroadcastNodeId)
     * @param source_node_id Source node ID from packet
     * @param target_node_id Node to check for routing
     * @return true if packet should be sent to target_node_id
     */
    bool ShouldRouteToNode(u32 dest_node_id, u32 source_node_id, u32 target_node_id) const;

    /**
     * @brief Get local node ID (this client's node)
     *
     * @return Local node ID (0xFF if not set)
     */
    u8 GetLocalNodeId() const { return m_local_node_id; }

    /**
     * @brief Set local node ID
     *
     * @param node_id Local node ID
     */
    void SetLocalNodeId(u8 node_id) { m_local_node_id = node_id; }

private:
    mutable os::SdkMutex m_mutex;       ///< Thread safety mutex
    NodeEntry m_nodes[MaxNodes];        ///< Node entries
    u8 m_local_node_id;                 ///< This client's node ID
};

} // namespace ams::mitm::ldn

/**
 * @file ldn_proxy_buffer.cpp
 * @brief Ring buffer implementation for LDN proxy data
 *
 * Implements the packet queue used to buffer proxy data between
 * the network thread (producer) and game thread (consumer).
 *
 * ## Memory Management
 * - Fixed-size buffer allocated at construction
 * - No dynamic memory allocation during operation
 * - Wraps around using ring buffer semantics
 *
 * ## Overflow Handling
 * When buffer is full, Write() returns false. The caller should
 * handle this by either:
 * - Dropping the packet (acceptable for game data)
 * - Waiting for space (may cause latency)
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "ldn_proxy_buffer.hpp"
#include <cstring>

namespace ams::mitm::ldn {

/**
 * @brief Constructor - initializes empty buffer
 *
 * All indices start at 0, buffer is empty.
 */
LdnProxyBuffer::LdnProxyBuffer()
    : m_mutex()
    , m_packets{}
    , m_packet_read_idx(0)
    , m_packet_write_idx(0)
    , m_packet_count(0)
    , m_data_buffer{}
    , m_data_read_pos(0)
    , m_data_write_pos(0)
{
}

/**
 * @brief Write a packet to the buffer
 *
 * Copies the header and data into the ring buffer.
 *
 * ## Algorithm
 * 1. Check if packet queue is full
 * 2. Check if data buffer has enough space
 * 3. Copy data to ring buffer (handle wrap-around)
 * 4. Store packet metadata
 * 5. Update write indices
 *
 * @param header Proxy data header (8 bytes)
 * @param data Packet payload
 * @param size Payload size (must be <= MaxPacketDataSize)
 * @return true if packet was queued successfully
 */
bool LdnProxyBuffer::Write(const ryu_ldn::protocol::ProxyDataHeader& header,
                           const u8* data, size_t size) {
    // Validate size
    if (size > MaxPacketDataSize) {
        return false;
    }

    std::scoped_lock lk(m_mutex);

    // Check if packet queue is full
    if (m_packet_count >= MaxQueuedPackets) {
        return false;
    }

    // Calculate available data space
    // For simplicity, use linear space check (no wrap for single packet)
    size_t available = BufferSize - m_data_write_pos;
    if (available < size) {
        // Not enough contiguous space, wrap to start if possible
        if (m_data_read_pos > size) {
            m_data_write_pos = 0;
        } else {
            // Buffer full
            return false;
        }
    }

    // Store packet metadata
    PacketEntry& entry = m_packets[m_packet_write_idx];
    entry.header = header;
    entry.data_size = size;
    entry.data_offset = m_data_write_pos;

    // Copy data to buffer
    if (size > 0 && data != nullptr) {
        std::memcpy(m_data_buffer + m_data_write_pos, data, size);
    }

    // Update indices
    m_data_write_pos += size;
    m_packet_write_idx = (m_packet_write_idx + 1) % MaxQueuedPackets;
    m_packet_count++;

    return true;
}

/**
 * @brief Read a packet from the buffer
 *
 * Copies the oldest packet's header and data to the output buffers.
 *
 * ## Algorithm
 * 1. Check if queue is empty
 * 2. Get packet metadata from read position
 * 3. Copy header to output
 * 4. Copy data to output (respecting max_size)
 * 5. Update read indices
 *
 * @param header Output: packet header
 * @param data Output: payload buffer
 * @param size Output: actual payload size copied
 * @param max_size Maximum bytes to copy to data buffer
 * @return true if packet was read, false if queue empty
 */
bool LdnProxyBuffer::Read(ryu_ldn::protocol::ProxyDataHeader& header,
                          u8* data, size_t& size, size_t max_size) {
    std::scoped_lock lk(m_mutex);

    // Check if queue is empty
    if (m_packet_count == 0) {
        return false;
    }

    // Get packet metadata
    const PacketEntry& entry = m_packets[m_packet_read_idx];

    // Copy header
    header = entry.header;

    // Determine how much data to copy
    size = (entry.data_size <= max_size) ? entry.data_size : max_size;

    // Copy data
    if (size > 0 && data != nullptr) {
        std::memcpy(data, m_data_buffer + entry.data_offset, size);
    }

    // Update read index to after this packet's data
    // This reclaims the data space for future writes
    m_data_read_pos = entry.data_offset + entry.data_size;

    // Update packet queue indices
    m_packet_read_idx = (m_packet_read_idx + 1) % MaxQueuedPackets;
    m_packet_count--;

    // If queue is now empty, reset data positions to start
    if (m_packet_count == 0) {
        m_data_read_pos = 0;
        m_data_write_pos = 0;
    }

    return true;
}

/**
 * @brief Peek at next packet without removing it
 *
 * Allows inspection of the next packet's metadata without
 * consuming it from the queue.
 *
 * @param header Output: packet header
 * @param size Output: payload size
 * @return true if packet available
 */
bool LdnProxyBuffer::Peek(ryu_ldn::protocol::ProxyDataHeader& header, size_t& size) const {
    std::scoped_lock lk(m_mutex);

    if (m_packet_count == 0) {
        return false;
    }

    const PacketEntry& entry = m_packets[m_packet_read_idx];
    header = entry.header;
    size = entry.data_size;

    return true;
}

/**
 * @brief Get number of packets in queue
 *
 * @return Number of packets waiting to be read
 */
size_t LdnProxyBuffer::GetPendingCount() const {
    std::scoped_lock lk(m_mutex);
    return m_packet_count;
}

/**
 * @brief Check if buffer is empty
 *
 * @return true if no packets in queue
 */
bool LdnProxyBuffer::IsEmpty() const {
    std::scoped_lock lk(m_mutex);
    return m_packet_count == 0;
}

/**
 * @brief Clear all queued packets
 *
 * Discards all pending packets and resets buffer to empty state.
 * Called when:
 * - Connection is lost
 * - Game requests clear
 * - Session ends
 */
void LdnProxyBuffer::Reset() {
    std::scoped_lock lk(m_mutex);

    m_packet_read_idx = 0;
    m_packet_write_idx = 0;
    m_packet_count = 0;
    m_data_read_pos = 0;
    m_data_write_pos = 0;
}

/**
 * @brief Get total bytes used in buffer
 *
 * Returns approximate bytes used in the data buffer.
 * Useful for monitoring buffer fill level.
 *
 * @return Bytes used (may wrap, so approximate)
 */
size_t LdnProxyBuffer::GetUsedBytes() const {
    std::scoped_lock lk(m_mutex);

    if (m_data_write_pos >= m_data_read_pos) {
        return m_data_write_pos - m_data_read_pos;
    } else {
        // Wrapped around
        return BufferSize - m_data_read_pos + m_data_write_pos;
    }
}

} // namespace ams::mitm::ldn

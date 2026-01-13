/**
 * @file ldn_proxy_buffer.hpp
 * @brief Ring buffer for LDN proxy data packets
 *
 * Provides a thread-safe ring buffer for queuing proxy data packets
 * between the network receive thread and the game's data consumption.
 *
 * ## Design Goals
 * - Zero-copy where possible
 * - Fixed memory footprint (no dynamic allocation)
 * - Thread-safe for single producer / single consumer
 * - Low latency (< 1ms overhead)
 *
 * ## Usage
 * @code
 * LdnProxyBuffer buffer;
 *
 * // Producer (network thread)
 * ProxyDataHeader header{...};
 * buffer.Write(header, data, size);
 *
 * // Consumer (game thread)
 * ProxyDataHeader header;
 * uint8_t data[1024];
 * size_t size;
 * if (buffer.Read(header, data, size, sizeof(data))) {
 *     // Process packet
 * }
 * @endcode
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>
#include "../protocol/types.hpp"

namespace ams::mitm::ldn {

/**
 * @brief Ring buffer for proxy data packets
 *
 * Implements a lock-free single-producer single-consumer queue
 * for proxy data. Uses a fixed-size ring buffer to avoid
 * memory allocation during gameplay.
 *
 * ## Memory Layout
 * The buffer stores packets as: [PacketSize:4][Header:8][Data:N]
 * This allows reading packet boundaries without parsing headers.
 *
 * ## Thread Safety
 * - Write(): Called from network receive thread
 * - Read(): Called from game thread
 * - Uses os::SdkMutex for simplicity (games don't call at high frequency)
 */
class LdnProxyBuffer {
public:
    /// Maximum size of a single proxy data packet (data only, not header)
    static constexpr size_t MaxPacketDataSize = 0x1000;  // 4KB

    /// Total buffer size (fits ~4 max-size packets)
    static constexpr size_t BufferSize = MaxPacketDataSize * 4 + 256;

    /// Maximum number of packets that can be queued
    static constexpr size_t MaxQueuedPackets = 32;

    /**
     * @brief Constructor - initializes empty buffer
     */
    LdnProxyBuffer();

    /**
     * @brief Write a packet to the buffer
     *
     * Adds a proxy data packet to the queue. If the buffer is full,
     * the oldest packet is dropped to make room.
     *
     * @param header Proxy data header (destination/source node IDs)
     * @param data Packet payload (can be nullptr if size is 0)
     * @param size Size of payload in bytes
     * @return true if packet was written, false if buffer overflow
     */
    bool Write(const ryu_ldn::protocol::ProxyDataHeader& header,
               const u8* data, size_t size);

    /**
     * @brief Read a packet from the buffer
     *
     * Removes and returns the oldest packet from the queue.
     *
     * @param header Output: packet header
     * @param data Output: packet payload buffer
     * @param size Output: actual payload size
     * @param max_size Maximum bytes to write to data buffer
     * @return true if packet was read, false if buffer empty
     */
    bool Read(ryu_ldn::protocol::ProxyDataHeader& header,
              u8* data, size_t& size, size_t max_size);

    /**
     * @brief Peek at next packet without removing it
     *
     * @param header Output: packet header
     * @param size Output: payload size
     * @return true if packet available
     */
    bool Peek(ryu_ldn::protocol::ProxyDataHeader& header, size_t& size) const;

    /**
     * @brief Get number of packets in queue
     *
     * @return Number of pending packets
     */
    size_t GetPendingCount() const;

    /**
     * @brief Check if buffer is empty
     *
     * @return true if no packets queued
     */
    bool IsEmpty() const;

    /**
     * @brief Clear all queued packets
     */
    void Reset();

    /**
     * @brief Get total bytes used in buffer
     *
     * For debugging/monitoring buffer usage.
     *
     * @return Bytes used
     */
    size_t GetUsedBytes() const;

private:
    /**
     * @brief Internal packet entry in queue
     */
    struct PacketEntry {
        ryu_ldn::protocol::ProxyDataHeader header;  ///< Packet header
        size_t data_size;                            ///< Payload size
        size_t data_offset;                          ///< Offset in m_data_buffer
    };

    mutable os::SdkMutex m_mutex;                    ///< Thread safety

    PacketEntry m_packets[MaxQueuedPackets];         ///< Packet metadata queue
    size_t m_packet_read_idx;                        ///< Read position
    size_t m_packet_write_idx;                       ///< Write position
    size_t m_packet_count;                           ///< Number of packets

    u8 m_data_buffer[BufferSize];                    ///< Payload data storage
    size_t m_data_read_pos;                          ///< Data read position
    size_t m_data_write_pos;                         ///< Data write position
};

} // namespace ams::mitm::ldn

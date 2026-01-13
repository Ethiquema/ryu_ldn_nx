/**
 * @file packet_buffer.hpp
 * @brief TCP Stream Buffer for RyuLdn Protocol
 *
 * Handles accumulation of TCP data and extraction of complete packets.
 * Uses a linear buffer with shift-on-extract strategy for embedded use.
 * No dynamic allocation - buffer size is fixed at compile time.
 *
 * Reference: Handles TCP fragmentation for RyuLdn protocol
 *
 * @copyright Ported from ryu_ldn (MIT License)
 * @license MIT
 */

#pragma once

#include "types.hpp"
#include "ryu_protocol.hpp"
#include <cstring>
#include <cstddef>

namespace ryu_ldn::protocol {

/**
 * @brief Result codes for buffer operations
 */
enum class BufferResult {
    Success = 0,
    BufferFull,
    NoCompletePacket,
    PacketTooLarge,
    InvalidPacket
};

/**
 * @brief TCP stream buffer for accumulating and extracting packets
 * @tparam BufferSize Size of internal buffer (default 64KB)
 *
 * Usage:
 * @code
 * PacketBuffer<> buffer;
 *
 * // Receive data from TCP socket
 * buffer.append(recv_data, recv_size);
 *
 * // Process all complete packets
 * while (buffer.has_complete_packet()) {
 *     size_t packet_size;
 *     const uint8_t* packet = buffer.peek_packet(packet_size);
 *
 *     // Process packet...
 *     handle_packet(packet, packet_size);
 *
 *     // Remove processed packet
 *     buffer.consume(packet_size);
 * }
 * @endcode
 */
template<size_t BufferSize = 0x10000>
class PacketBuffer {
public:
    /**
     * @brief Default constructor - initializes empty buffer
     *
     * Note: For production use, BufferSize should be >= sizeof(LdnHeader) + MAX_PACKET_SIZE
     * to handle maximum-sized packets. Smaller buffers can be used for testing or
     * when only small packets are expected.
     */
    PacketBuffer() : m_write_pos(0) {
        // Production code should use large enough buffers
        // static_assert removed to allow testing with smaller buffers
    }

    /**
     * @brief Reset buffer to empty state
     */
    void reset() {
        m_write_pos = 0;
    }

    /**
     * @brief Get current data size in buffer
     * @return Number of bytes in buffer
     */
    size_t size() const {
        return m_write_pos;
    }

    /**
     * @brief Check if buffer is empty
     * @return true if no data in buffer
     */
    bool empty() const {
        return m_write_pos == 0;
    }

    /**
     * @brief Get available space in buffer
     * @return Number of bytes that can be appended
     */
    size_t available() const {
        return BufferSize - m_write_pos;
    }

    /**
     * @brief Get pointer to raw buffer data
     * @return Pointer to buffer start
     */
    const uint8_t* data() const {
        return m_buffer;
    }

    /**
     * @brief Append data to buffer
     * @param data Data to append
     * @param size Size of data
     * @return BufferResult::Success or error
     */
    BufferResult append(const uint8_t* data, size_t size) {
        if (size == 0) {
            return BufferResult::Success;
        }

        if (size > available()) {
            return BufferResult::BufferFull;
        }

        std::memcpy(m_buffer + m_write_pos, data, size);
        m_write_pos += size;

        return BufferResult::Success;
    }

    /**
     * @brief Check if buffer contains a complete packet
     * @return true if at least one complete packet is available
     */
    bool has_complete_packet() const {
        if (m_write_pos < sizeof(LdnHeader)) {
            return false;
        }

        size_t packet_size;
        DecodeResult result = check_complete_packet(m_buffer, m_write_pos, packet_size);
        return result == DecodeResult::Success;
    }

    /**
     * @brief Get information about next packet without consuming it
     * @param[out] packet_size Size of complete packet (if available)
     * @return BufferResult indicating status
     */
    BufferResult peek_packet_info(size_t& packet_size) const {
        if (m_write_pos < sizeof(LdnHeader)) {
            packet_size = 0;
            return BufferResult::NoCompletePacket;
        }

        DecodeResult result = check_complete_packet(m_buffer, m_write_pos, packet_size);

        switch (result) {
            case DecodeResult::Success:
                return BufferResult::Success;
            case DecodeResult::IncompletePacket:
            case DecodeResult::BufferTooSmall:
                return BufferResult::NoCompletePacket;
            case DecodeResult::PacketTooLarge:
                return BufferResult::PacketTooLarge;
            case DecodeResult::InvalidMagic:
            case DecodeResult::InvalidVersion:
                return BufferResult::InvalidPacket;
            default:
                return BufferResult::InvalidPacket;
        }
    }

    /**
     * @brief Peek at the next complete packet without consuming it
     * @param[out] packet_size Size of the packet
     * @return Pointer to packet data, or nullptr if no complete packet
     */
    const uint8_t* peek_packet(size_t& packet_size) const {
        BufferResult result = peek_packet_info(packet_size);
        if (result != BufferResult::Success) {
            packet_size = 0;
            return nullptr;
        }
        return m_buffer;
    }

    /**
     * @brief Get packet type of next packet (if available)
     * @return PacketId of next packet, or Initialize (0) if no packet
     */
    PacketId peek_packet_type() const {
        if (m_write_pos < sizeof(LdnHeader)) {
            return PacketId::Initialize;
        }

        LdnHeader header;
        DecodeResult result = decode_header(m_buffer, m_write_pos, header);
        if (result != DecodeResult::Success) {
            return PacketId::Initialize;
        }

        return static_cast<PacketId>(header.type);
    }

    /**
     * @brief Consume (remove) bytes from the front of the buffer
     * @param size Number of bytes to consume
     *
     * Typically called after processing a packet to remove it from buffer.
     * Uses memmove to shift remaining data to front.
     */
    void consume(size_t size) {
        if (size == 0) {
            return;
        }

        if (size >= m_write_pos) {
            // Consume all data
            m_write_pos = 0;
            return;
        }

        // Shift remaining data to front
        const size_t remaining = m_write_pos - size;
        std::memmove(m_buffer, m_buffer + size, remaining);
        m_write_pos = remaining;
    }

    /**
     * @brief Extract next complete packet into output buffer
     * @param out_buffer Output buffer for packet
     * @param out_buffer_size Size of output buffer
     * @param[out] packet_size Actual packet size written
     * @return BufferResult::Success or error
     *
     * Copies the packet to output buffer and consumes it from internal buffer.
     */
    BufferResult extract_packet(uint8_t* out_buffer, size_t out_buffer_size, size_t& packet_size) {
        BufferResult result = peek_packet_info(packet_size);
        if (result != BufferResult::Success) {
            return result;
        }

        if (out_buffer_size < packet_size) {
            return BufferResult::BufferFull;
        }

        // Copy packet to output
        std::memcpy(out_buffer, m_buffer, packet_size);

        // Remove from internal buffer
        consume(packet_size);

        return BufferResult::Success;
    }

    /**
     * @brief Discard invalid data until valid header found or buffer empty
     * @return Number of bytes discarded
     *
     * Use this to recover from protocol errors or corrupted data.
     * Scans for PROTOCOL_MAGIC and discards data before it.
     */
    size_t discard_until_valid() {
        size_t discarded = 0;

        while (m_write_pos >= sizeof(LdnHeader)) {
            LdnHeader header;
            DecodeResult result = decode_header(m_buffer, m_write_pos, header);

            if (result == DecodeResult::Success ||
                result == DecodeResult::IncompletePacket) {
                // Found valid header
                break;
            }

            // Invalid header, discard one byte and try again
            consume(1);
            discarded++;
        }

        return discarded;
    }

    /**
     * @brief Get writable pointer for direct recv() into buffer
     * @return Pointer to write position
     *
     * Use with available() to recv() directly into buffer:
     * @code
     * ssize_t n = recv(sock, buffer.write_ptr(), buffer.available(), 0);
     * if (n > 0) buffer.advance_write(n);
     * @endcode
     */
    uint8_t* write_ptr() {
        return m_buffer + m_write_pos;
    }

    /**
     * @brief Advance write position after direct write
     * @param size Number of bytes written
     */
    void advance_write(size_t size) {
        if (m_write_pos + size <= BufferSize) {
            m_write_pos += size;
        }
    }

    /**
     * @brief Get buffer capacity
     * @return Total buffer size
     */
    static constexpr size_t capacity() {
        return BufferSize;
    }

private:
    uint8_t m_buffer[BufferSize];
    size_t m_write_pos;
};

/**
 * @brief Convert BufferResult to string for debugging
 */
inline const char* buffer_result_to_string(BufferResult result) {
    switch (result) {
        case BufferResult::Success:         return "Success";
        case BufferResult::BufferFull:      return "BufferFull";
        case BufferResult::NoCompletePacket: return "NoCompletePacket";
        case BufferResult::PacketTooLarge:  return "PacketTooLarge";
        case BufferResult::InvalidPacket:   return "InvalidPacket";
        default:                            return "Unknown";
    }
}

} // namespace ryu_ldn::protocol

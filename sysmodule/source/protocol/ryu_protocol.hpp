/**
 * @file ryu_protocol.hpp
 * @brief RyuLdn Protocol Encoder/Decoder
 *
 * Provides functions to encode and decode packets for communication
 * with ryu_ldn servers. All encoding is done in-place without dynamic
 * allocation for embedded use.
 *
 * Reference: LdnServer/Network/RyuLdnProtocol.cs
 *
 * @copyright Ported from ryu_ldn (MIT License)
 * @license MIT
 */

#pragma once

#include "types.hpp"
#include <cstring>
#include <cstddef>

namespace ryu_ldn::protocol {

// ============================================================================
// Result Codes
// ============================================================================

enum class EncodeResult {
    Success = 0,
    BufferTooSmall,
    InvalidPacketId
};

enum class DecodeResult {
    Success = 0,
    BufferTooSmall,
    InvalidMagic,
    InvalidVersion,
    PacketTooLarge,
    IncompletePacket
};

// ============================================================================
// Encoder Functions
// ============================================================================

/**
 * @brief Get the size needed for a packet with given payload size
 * @param payload_size Size of the payload data
 * @return Total packet size (header + payload)
 */
constexpr size_t get_packet_size(size_t payload_size) {
    return sizeof(LdnHeader) + payload_size;
}

/**
 * @brief Get the size needed for a packet with struct payload
 * @tparam T Payload structure type
 * @return Total packet size (header + sizeof(T))
 */
template<typename T>
constexpr size_t get_packet_size() {
    return sizeof(LdnHeader) + sizeof(T);
}

/**
 * @brief Encode a packet header into buffer
 * @param buffer Output buffer (must be at least sizeof(LdnHeader))
 * @param type Packet type
 * @param data_size Size of payload following header
 * @return Number of bytes written (sizeof(LdnHeader))
 */
inline size_t encode_header(uint8_t* buffer, PacketId type, int32_t data_size) {
    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.type = static_cast<uint8_t>(type);
    header.version = PROTOCOL_VERSION;
    header.data_size = data_size;

    std::memcpy(buffer, &header, sizeof(LdnHeader));
    return sizeof(LdnHeader);
}

/**
 * @brief Encode a packet with no payload (header only)
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @param type Packet type
 * @param[out] out_size Number of bytes written
 * @return EncodeResult::Success or error
 */
inline EncodeResult encode(uint8_t* buffer, size_t buffer_size, PacketId type, size_t& out_size) {
    constexpr size_t required = sizeof(LdnHeader);
    if (buffer_size < required) {
        out_size = 0;
        return EncodeResult::BufferTooSmall;
    }

    out_size = encode_header(buffer, type, 0);
    return EncodeResult::Success;
}

/**
 * @brief Encode a packet with struct payload
 * @tparam T Payload structure type
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @param type Packet type
 * @param payload Payload data
 * @param[out] out_size Number of bytes written
 * @return EncodeResult::Success or error
 */
template<typename T>
EncodeResult encode(uint8_t* buffer, size_t buffer_size, PacketId type, const T& payload, size_t& out_size) {
    const size_t required = get_packet_size<T>();
    if (buffer_size < required) {
        out_size = 0;
        return EncodeResult::BufferTooSmall;
    }

    // Write header
    size_t offset = encode_header(buffer, type, sizeof(T));

    // Write payload
    std::memcpy(buffer + offset, &payload, sizeof(T));
    out_size = required;

    return EncodeResult::Success;
}

/**
 * @brief Encode a packet with struct payload and extra data
 * @tparam T Payload structure type
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @param type Packet type
 * @param payload Payload structure
 * @param extra_data Additional data after payload
 * @param extra_size Size of extra data
 * @param[out] out_size Number of bytes written
 * @return EncodeResult::Success or error
 */
template<typename T>
EncodeResult encode_with_data(uint8_t* buffer, size_t buffer_size, PacketId type,
                              const T& payload, const uint8_t* extra_data, size_t extra_size,
                              size_t& out_size) {
    const size_t required = sizeof(LdnHeader) + sizeof(T) + extra_size;
    if (buffer_size < required) {
        out_size = 0;
        return EncodeResult::BufferTooSmall;
    }

    // Write header with combined size
    size_t offset = encode_header(buffer, type, sizeof(T) + static_cast<int32_t>(extra_size));

    // Write payload
    std::memcpy(buffer + offset, &payload, sizeof(T));
    offset += sizeof(T);

    // Write extra data
    if (extra_data && extra_size > 0) {
        std::memcpy(buffer + offset, extra_data, extra_size);
    }

    out_size = required;
    return EncodeResult::Success;
}

/**
 * @brief Encode raw data packet (header + raw bytes)
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @param type Packet type
 * @param data Raw data
 * @param data_size Size of raw data
 * @param[out] out_size Number of bytes written
 * @return EncodeResult::Success or error
 */
inline EncodeResult encode_raw(uint8_t* buffer, size_t buffer_size, PacketId type,
                               const uint8_t* data, size_t data_size, size_t& out_size) {
    const size_t required = sizeof(LdnHeader) + data_size;
    if (buffer_size < required) {
        out_size = 0;
        return EncodeResult::BufferTooSmall;
    }

    // Write header
    size_t offset = encode_header(buffer, type, static_cast<int32_t>(data_size));

    // Write data
    if (data && data_size > 0) {
        std::memcpy(buffer + offset, data, data_size);
    }

    out_size = required;
    return EncodeResult::Success;
}

// ============================================================================
// Convenience Encode Functions
// ============================================================================

/**
 * @brief Encode Initialize message
 */
inline EncodeResult encode_initialize(uint8_t* buffer, size_t buffer_size,
                                      const SessionId& id, const MacAddress& mac,
                                      size_t& out_size) {
    InitializeMessage msg{};
    msg.id = id;
    msg.mac_address = mac;
    return encode(buffer, buffer_size, PacketId::Initialize, msg, out_size);
}

/**
 * @brief Encode Passphrase message
 */
inline EncodeResult encode_passphrase(uint8_t* buffer, size_t buffer_size,
                                      const uint8_t* passphrase, size_t passphrase_len,
                                      size_t& out_size) {
    PassphraseMessage msg{};
    std::memset(msg.passphrase, 0, sizeof(msg.passphrase));
    if (passphrase && passphrase_len > 0) {
        size_t copy_len = passphrase_len < 128 ? passphrase_len : 128;
        std::memcpy(msg.passphrase, passphrase, copy_len);
    }
    return encode(buffer, buffer_size, PacketId::Passphrase, msg, out_size);
}

/**
 * @brief Encode Ping message
 */
inline EncodeResult encode_ping(uint8_t* buffer, size_t buffer_size,
                                uint8_t requester, uint8_t id, size_t& out_size) {
    PingMessage msg{};
    msg.requester = requester;
    msg.id = id;
    return encode(buffer, buffer_size, PacketId::Ping, msg, out_size);
}

/**
 * @brief Encode Disconnect message
 *
 * @param disconnect_ip IP address of disconnecting client (0 = self)
 */
inline EncodeResult encode_disconnect(uint8_t* buffer, size_t buffer_size,
                                      uint32_t disconnect_ip, size_t& out_size) {
    DisconnectMessage msg{};
    msg.disconnect_ip = disconnect_ip;
    return encode(buffer, buffer_size, PacketId::Disconnect, msg, out_size);
}

/**
 * @brief Encode Scan request
 */
inline EncodeResult encode_scan(uint8_t* buffer, size_t buffer_size,
                                const ScanFilterFull& filter, size_t& out_size) {
    return encode(buffer, buffer_size, PacketId::Scan, filter, out_size);
}

/**
 * @brief Encode Connect request
 */
inline EncodeResult encode_connect(uint8_t* buffer, size_t buffer_size,
                                   const ConnectRequest& request, size_t& out_size) {
    return encode(buffer, buffer_size, PacketId::Connect, request, out_size);
}

/**
 * @brief Encode CreateAccessPoint request with advertise data
 */
inline EncodeResult encode_create_access_point(uint8_t* buffer, size_t buffer_size,
                                               const CreateAccessPointRequest& request,
                                               const uint8_t* advertise_data, size_t advertise_size,
                                               size_t& out_size) {
    return encode_with_data(buffer, buffer_size, PacketId::CreateAccessPoint,
                            request, advertise_data, advertise_size, out_size);
}

/**
 * @brief Encode SetAcceptPolicy request
 */
inline EncodeResult encode_set_accept_policy(uint8_t* buffer, size_t buffer_size,
                                             AcceptPolicy policy, size_t& out_size) {
    SetAcceptPolicyRequest msg{};
    msg.accept_policy = static_cast<uint32_t>(policy);
    return encode(buffer, buffer_size, PacketId::SetAcceptPolicy, msg, out_size);
}

/**
 * @brief Encode SetAdvertiseData request
 */
inline EncodeResult encode_set_advertise_data(uint8_t* buffer, size_t buffer_size,
                                              const uint8_t* data, size_t data_size,
                                              size_t& out_size) {
    return encode_raw(buffer, buffer_size, PacketId::SetAdvertiseData, data, data_size, out_size);
}

/**
 * @brief Encode ProxyData packet
 *
 * @param info Proxy connection info (source/dest addressing)
 * @param data Payload data to send
 * @param data_size Size of payload
 */
inline EncodeResult encode_proxy_data(uint8_t* buffer, size_t buffer_size,
                                      const ProxyInfo& info,
                                      const uint8_t* data, size_t data_size,
                                      size_t& out_size) {
    ProxyDataHeader header{};
    header.info = info;
    header.data_length = static_cast<uint32_t>(data_size);
    return encode_with_data(buffer, buffer_size, PacketId::ProxyData,
                            header, data, data_size, out_size);
}

/**
 * @brief Encode ScanReplyEnd (no payload)
 */
inline EncodeResult encode_scan_reply_end(uint8_t* buffer, size_t buffer_size, size_t& out_size) {
    return encode(buffer, buffer_size, PacketId::ScanReplyEnd, out_size);
}

/**
 * @brief Encode RejectReply (no payload)
 */
inline EncodeResult encode_reject_reply(uint8_t* buffer, size_t buffer_size, size_t& out_size) {
    return encode(buffer, buffer_size, PacketId::RejectReply, out_size);
}

/**
 * @brief Encode NetworkInfo (for Connected, SyncNetwork, ScanReply)
 */
inline EncodeResult encode_network_info(uint8_t* buffer, size_t buffer_size,
                                        PacketId type, const NetworkInfo& info,
                                        size_t& out_size) {
    return encode(buffer, buffer_size, type, info, out_size);
}

// ============================================================================
// Decoder Functions
// ============================================================================

/**
 * @brief Check if buffer contains enough data for a header
 * @param buffer_size Available data size
 * @return true if header can be read
 */
constexpr bool has_header(size_t buffer_size) {
    return buffer_size >= sizeof(LdnHeader);
}

/**
 * @brief Decode and validate a packet header from buffer
 * @param buffer Input buffer
 * @param buffer_size Size of input buffer
 * @param[out] header Decoded header
 * @return DecodeResult::Success or error
 */
inline DecodeResult decode_header(const uint8_t* buffer, size_t buffer_size, LdnHeader& header) {
    if (buffer_size < sizeof(LdnHeader)) {
        return DecodeResult::BufferTooSmall;
    }

    std::memcpy(&header, buffer, sizeof(LdnHeader));

    // Validate magic number
    if (header.magic != PROTOCOL_MAGIC) {
        return DecodeResult::InvalidMagic;
    }

    // Validate version
    if (header.version != PROTOCOL_VERSION) {
        return DecodeResult::InvalidVersion;
    }

    // Validate data size (prevent excessive allocation)
    if (header.data_size < 0 || static_cast<size_t>(header.data_size) > MAX_PACKET_SIZE) {
        return DecodeResult::PacketTooLarge;
    }

    return DecodeResult::Success;
}

/**
 * @brief Check if buffer contains a complete packet
 * @param buffer Input buffer
 * @param buffer_size Size of input buffer
 * @param[out] packet_size Total size of complete packet (if available)
 * @return DecodeResult::Success if complete packet available
 */
inline DecodeResult check_complete_packet(const uint8_t* buffer, size_t buffer_size, size_t& packet_size) {
    LdnHeader header;
    DecodeResult result = decode_header(buffer, buffer_size, header);
    if (result != DecodeResult::Success) {
        packet_size = 0;
        return result;
    }

    packet_size = sizeof(LdnHeader) + static_cast<size_t>(header.data_size);

    if (buffer_size < packet_size) {
        return DecodeResult::IncompletePacket;
    }

    return DecodeResult::Success;
}

/**
 * @brief Get packet type from buffer (assumes header is valid)
 * @param buffer Input buffer containing valid header
 * @return PacketId from header
 */
inline PacketId get_packet_type(const uint8_t* buffer) {
    LdnHeader header;
    std::memcpy(&header, buffer, sizeof(LdnHeader));
    return static_cast<PacketId>(header.type);
}

/**
 * @brief Get payload size from buffer (assumes header is valid)
 * @param buffer Input buffer containing valid header
 * @return Payload size from header
 */
inline int32_t get_payload_size(const uint8_t* buffer) {
    LdnHeader header;
    std::memcpy(&header, buffer, sizeof(LdnHeader));
    return header.data_size;
}

/**
 * @brief Get pointer to payload data
 * @param buffer Input buffer containing packet
 * @return Pointer to payload (after header)
 */
inline const uint8_t* get_payload_ptr(const uint8_t* buffer) {
    return buffer + sizeof(LdnHeader);
}

/**
 * @brief Decode a packet with struct payload
 * @tparam T Payload structure type
 * @param buffer Input buffer
 * @param buffer_size Size of input buffer
 * @param[out] header Decoded header
 * @param[out] payload Decoded payload
 * @return DecodeResult::Success or error
 */
template<typename T>
DecodeResult decode(const uint8_t* buffer, size_t buffer_size, LdnHeader& header, T& payload) {
    // Decode and validate header
    DecodeResult result = decode_header(buffer, buffer_size, header);
    if (result != DecodeResult::Success) {
        return result;
    }

    // Check payload size matches expected
    const size_t expected_size = sizeof(LdnHeader) + sizeof(T);
    if (buffer_size < expected_size) {
        return DecodeResult::BufferTooSmall;
    }

    // Copy payload
    std::memcpy(&payload, buffer + sizeof(LdnHeader), sizeof(T));
    return DecodeResult::Success;
}

/**
 * @brief Decode a packet with struct payload and extra data
 * @tparam T Payload structure type
 * @param buffer Input buffer
 * @param buffer_size Size of input buffer
 * @param[out] header Decoded header
 * @param[out] payload Decoded payload structure
 * @param[out] extra_data Pointer to extra data (points into buffer)
 * @param[out] extra_size Size of extra data
 * @return DecodeResult::Success or error
 */
template<typename T>
DecodeResult decode_with_data(const uint8_t* buffer, size_t buffer_size,
                               LdnHeader& header, T& payload,
                               const uint8_t*& extra_data, size_t& extra_size) {
    // Decode and validate header
    DecodeResult result = decode_header(buffer, buffer_size, header);
    if (result != DecodeResult::Success) {
        extra_data = nullptr;
        extra_size = 0;
        return result;
    }

    // Check minimum size for structure
    const size_t min_size = sizeof(LdnHeader) + sizeof(T);
    if (buffer_size < min_size) {
        extra_data = nullptr;
        extra_size = 0;
        return DecodeResult::BufferTooSmall;
    }

    // Copy payload structure
    std::memcpy(&payload, buffer + sizeof(LdnHeader), sizeof(T));

    // Calculate extra data
    const size_t total_payload = static_cast<size_t>(header.data_size);
    if (total_payload > sizeof(T)) {
        extra_size = total_payload - sizeof(T);
        extra_data = buffer + sizeof(LdnHeader) + sizeof(T);

        // Verify buffer has enough data
        if (buffer_size < sizeof(LdnHeader) + total_payload) {
            extra_data = nullptr;
            extra_size = 0;
            return DecodeResult::IncompletePacket;
        }
    } else {
        extra_data = nullptr;
        extra_size = 0;
    }

    return DecodeResult::Success;
}

/**
 * @brief Decode raw data packet (header + raw bytes)
 * @param buffer Input buffer
 * @param buffer_size Size of input buffer
 * @param[out] header Decoded header
 * @param[out] data Pointer to raw data (points into buffer)
 * @param[out] data_size Size of raw data
 * @return DecodeResult::Success or error
 */
inline DecodeResult decode_raw(const uint8_t* buffer, size_t buffer_size,
                                LdnHeader& header,
                                const uint8_t*& data, size_t& data_size) {
    // Decode and validate header
    DecodeResult result = decode_header(buffer, buffer_size, header);
    if (result != DecodeResult::Success) {
        data = nullptr;
        data_size = 0;
        return result;
    }

    data_size = static_cast<size_t>(header.data_size);
    const size_t total_size = sizeof(LdnHeader) + data_size;

    if (buffer_size < total_size) {
        data = nullptr;
        data_size = 0;
        return DecodeResult::IncompletePacket;
    }

    data = (data_size > 0) ? (buffer + sizeof(LdnHeader)) : nullptr;
    return DecodeResult::Success;
}

// ============================================================================
// Convenience Decode Functions
// ============================================================================

/**
 * @brief Decode Initialize message
 */
inline DecodeResult decode_initialize(const uint8_t* buffer, size_t buffer_size,
                                       LdnHeader& header, InitializeMessage& msg) {
    return decode(buffer, buffer_size, header, msg);
}

/**
 * @brief Decode Passphrase message
 */
inline DecodeResult decode_passphrase(const uint8_t* buffer, size_t buffer_size,
                                       LdnHeader& header, PassphraseMessage& msg) {
    return decode(buffer, buffer_size, header, msg);
}

/**
 * @brief Decode Ping message
 */
inline DecodeResult decode_ping(const uint8_t* buffer, size_t buffer_size,
                                 LdnHeader& header, PingMessage& msg) {
    return decode(buffer, buffer_size, header, msg);
}

/**
 * @brief Decode Disconnect message
 */
inline DecodeResult decode_disconnect(const uint8_t* buffer, size_t buffer_size,
                                       LdnHeader& header, DisconnectMessage& msg) {
    return decode(buffer, buffer_size, header, msg);
}

/**
 * @brief Decode NetworkInfo (for Connected, SyncNetwork, ScanReply)
 */
inline DecodeResult decode_network_info(const uint8_t* buffer, size_t buffer_size,
                                         LdnHeader& header, NetworkInfo& info) {
    return decode(buffer, buffer_size, header, info);
}

/**
 * @brief Decode Scan request
 */
inline DecodeResult decode_scan(const uint8_t* buffer, size_t buffer_size,
                                 LdnHeader& header, ScanFilterFull& filter) {
    return decode(buffer, buffer_size, header, filter);
}

/**
 * @brief Decode Connect request
 */
inline DecodeResult decode_connect(const uint8_t* buffer, size_t buffer_size,
                                    LdnHeader& header, ConnectRequest& request) {
    return decode(buffer, buffer_size, header, request);
}

/**
 * @brief Decode CreateAccessPoint request with advertise data
 */
inline DecodeResult decode_create_access_point(const uint8_t* buffer, size_t buffer_size,
                                                LdnHeader& header, CreateAccessPointRequest& request,
                                                const uint8_t*& advertise_data, size_t& advertise_size) {
    return decode_with_data(buffer, buffer_size, header, request, advertise_data, advertise_size);
}

/**
 * @brief Decode SetAcceptPolicy request
 */
inline DecodeResult decode_set_accept_policy(const uint8_t* buffer, size_t buffer_size,
                                              LdnHeader& header, SetAcceptPolicyRequest& request) {
    return decode(buffer, buffer_size, header, request);
}

/**
 * @brief Decode SetAdvertiseData request
 */
inline DecodeResult decode_set_advertise_data(const uint8_t* buffer, size_t buffer_size,
                                               LdnHeader& header,
                                               const uint8_t*& data, size_t& data_size) {
    return decode_raw(buffer, buffer_size, header, data, data_size);
}

/**
 * @brief Decode ProxyData packet
 */
inline DecodeResult decode_proxy_data(const uint8_t* buffer, size_t buffer_size,
                                       LdnHeader& header, ProxyDataHeader& proxy_header,
                                       const uint8_t*& data, size_t& data_size) {
    return decode_with_data(buffer, buffer_size, header, proxy_header, data, data_size);
}

/**
 * @brief Decode ProxyConnect message
 */
inline DecodeResult decode_proxy_connect(const uint8_t* buffer, size_t buffer_size,
                                          LdnHeader& header, ProxyConnectRequest& msg) {
    return decode(buffer, buffer_size, header, msg);
}

/**
 * @brief Decode ProxyConnectReply message
 */
inline DecodeResult decode_proxy_connect_reply(const uint8_t* buffer, size_t buffer_size,
                                                LdnHeader& header, ProxyConnectResponse& msg) {
    return decode(buffer, buffer_size, header, msg);
}

/**
 * @brief Decode ProxyDisconnect message
 */
inline DecodeResult decode_proxy_disconnect(const uint8_t* buffer, size_t buffer_size,
                                             LdnHeader& header, ProxyDisconnectMessage& msg) {
    return decode(buffer, buffer_size, header, msg);
}

/**
 * @brief Decode RejectRequest
 */
inline DecodeResult decode_reject(const uint8_t* buffer, size_t buffer_size,
                                   LdnHeader& header, RejectRequest& request) {
    return decode(buffer, buffer_size, header, request);
}

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Convert DecodeResult to string for debugging
 */
inline const char* decode_result_to_string(DecodeResult result) {
    switch (result) {
        case DecodeResult::Success:          return "Success";
        case DecodeResult::BufferTooSmall:   return "BufferTooSmall";
        case DecodeResult::InvalidMagic:     return "InvalidMagic";
        case DecodeResult::InvalidVersion:   return "InvalidVersion";
        case DecodeResult::PacketTooLarge:   return "PacketTooLarge";
        case DecodeResult::IncompletePacket: return "IncompletePacket";
        default:                             return "Unknown";
    }
}

/**
 * @brief Convert EncodeResult to string for debugging
 */
inline const char* encode_result_to_string(EncodeResult result) {
    switch (result) {
        case EncodeResult::Success:        return "Success";
        case EncodeResult::BufferTooSmall: return "BufferTooSmall";
        case EncodeResult::InvalidPacketId: return "InvalidPacketId";
        default:                           return "Unknown";
    }
}

/**
 * @brief Convert PacketId to string for debugging
 */
inline const char* packet_id_to_string(PacketId id) {
    switch (id) {
        case PacketId::Initialize:            return "Initialize";
        case PacketId::Passphrase:            return "Passphrase";
        case PacketId::CreateAccessPoint:     return "CreateAccessPoint";
        case PacketId::CreateAccessPointPrivate: return "CreateAccessPointPrivate";
        case PacketId::ExternalProxy:         return "ExternalProxy";
        case PacketId::ExternalProxyToken:    return "ExternalProxyToken";
        case PacketId::ExternalProxyState:    return "ExternalProxyState";
        case PacketId::SyncNetwork:           return "SyncNetwork";
        case PacketId::Reject:                return "Reject";
        case PacketId::RejectReply:           return "RejectReply";
        case PacketId::Scan:                  return "Scan";
        case PacketId::ScanReply:             return "ScanReply";
        case PacketId::ScanReplyEnd:          return "ScanReplyEnd";
        case PacketId::Connect:               return "Connect";
        case PacketId::ConnectPrivate:        return "ConnectPrivate";
        case PacketId::Connected:             return "Connected";
        case PacketId::Disconnect:            return "Disconnect";
        case PacketId::ProxyConfig:           return "ProxyConfig";
        case PacketId::ProxyConnect:          return "ProxyConnect";
        case PacketId::ProxyConnectReply:     return "ProxyConnectReply";
        case PacketId::ProxyData:             return "ProxyData";
        case PacketId::ProxyDisconnect:       return "ProxyDisconnect";
        case PacketId::SetAcceptPolicy:       return "SetAcceptPolicy";
        case PacketId::SetAdvertiseData:      return "SetAdvertiseData";
        case PacketId::Ping:                  return "Ping";
        case PacketId::NetworkError:          return "NetworkError";
        default:                              return "Unknown";
    }
}

} // namespace ryu_ldn::protocol

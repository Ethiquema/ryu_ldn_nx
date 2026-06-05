/**
 * @file formatters.hpp
 * @brief Color and string formatting helpers for the ryu_ldn_nx overlay.
 *
 * Provides connection status/LDN state to string and color mappings,
 * passphrase display formatting, and random passphrase generation.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */
#pragma once

#include <cstring>
#include <cstdlib>
#include <ctime>

#include <tesla.hpp>

#include "ryu_ldn_ipc.h"

namespace ryu_ldn::overlay {

/** @brief Convert a RyuLdnConnectionStatus to a human-readable string. */
inline const char* ConnectionStatusToString(RyuLdnConnectionStatus status) {
    switch (status) {
        case RyuLdnStatus_Disconnected: return "Disconnected";
        case RyuLdnStatus_Connecting:   return "Connecting...";
        case RyuLdnStatus_Connected:    return "Connected";
        case RyuLdnStatus_Ready:        return "Ready";
        case RyuLdnStatus_Error:        return "Error";
        default:                        return "Unknown";
    }
}

/** @brief Return a color for a given RyuLdnConnectionStatus. */
inline tsl::Color StatusColor(RyuLdnConnectionStatus status) {
    switch (status) {
        case RyuLdnStatus_Ready:        return tsl::Color(0x0, 0xF, 0x0, 0xF);
        case RyuLdnStatus_Connected:    return tsl::Color(0x0, 0xF, 0x0, 0xF);
        case RyuLdnStatus_Connecting:   return tsl::Color(0xF, 0xF, 0x0, 0xF);
        case RyuLdnStatus_Disconnected: return tsl::Color(0x8, 0x8, 0x8, 0xF);
        case RyuLdnStatus_Error:        return tsl::Color(0xF, 0x0, 0x0, 0xF);
        default:                        return tsl::Color(0xF, 0xF, 0xF, 0xF);
    }
}

/** @brief Return a color for a given RyuLdnState. */
inline tsl::Color LdnStateColor(RyuLdnState state) {
    switch (state) {
        case RyuLdnState_AccessPointCreated:
        case RyuLdnState_StationConnected:  return tsl::Color(0x0, 0xF, 0x0, 0xF);
        case RyuLdnState_AccessPoint:
        case RyuLdnState_Station:           return tsl::Color(0xF, 0xF, 0x0, 0xF);
        case RyuLdnState_Error:             return tsl::Color(0xF, 0x0, 0x0, 0xF);
        default:                            return tsl::Color(0x8, 0x8, 0x8, 0xF);
    }
}

/**
 * @brief Format a passphrase for display.
 *
 * If the passphrase is a valid Ryujinx passphrase (16 chars starting with "Ryujinx-"),
 * display only the hex portion after the prefix. Otherwise show "(invalid)" or "---".
 */
inline void FormatPassphraseDisplay(const char* passphrase, char* buf, size_t bufSize) {
    if (passphrase == nullptr || passphrase[0] == '\0') {
        snprintf(buf, bufSize, "---");
    } else if (strlen(passphrase) == 16 && strncmp(passphrase, "Ryujinx-", 8) == 0) {
        snprintf(buf, bufSize, "%s", passphrase + 8);
    } else {
        snprintf(buf, bufSize, "(invalid)");
    }
}

/** @brief Generate a random passphrase in the format "Ryujinx-XXXXXXXX". */
inline void GenerateRandomPassphraseOverlay(char* out, size_t out_size) {
    if (out == nullptr || out_size < 17) {
        if (out != nullptr && out_size > 0) out[0] = '\0';
        return;
    }
    static bool seeded = false;
    if (!seeded) {
        srand(static_cast<unsigned>(time(nullptr)));
        seeded = true;
    }
    const char* hex_chars = "0123456789abcdef";
    strcpy(out, "Ryujinx-");
    for (int i = 0; i < 8; i++) {
        out[8 + i] = hex_chars[rand() % 16];
    }
    out[16] = '\0';
}

} // namespace ryu_ldn::overlay

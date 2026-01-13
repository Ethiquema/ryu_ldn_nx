/**
 * @file config_manager.cpp
 * @brief Global Configuration Manager implementation
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "config_manager.hpp"
#include <cstring>

namespace ryu_ldn::config {

// =============================================================================
// Singleton Instance
// =============================================================================

ConfigManager& ConfigManager::Instance() {
    static ConfigManager instance;
    return instance;
}

// =============================================================================
// Initialization
// =============================================================================

bool ConfigManager::Initialize(const char* config_path) {
    // Store config path
    if (config_path != nullptr) {
        std::strncpy(m_config_path, config_path, sizeof(m_config_path) - 1);
        m_config_path[sizeof(m_config_path) - 1] = '\0';
    } else {
        std::strncpy(m_config_path, CONFIG_PATH, sizeof(m_config_path) - 1);
        m_config_path[sizeof(m_config_path) - 1] = '\0';
    }

    // Load defaults first
    m_config = get_default_config();

    // Try to load from file
    ConfigResult result = load_config(m_config_path, m_config);

    // Success or FileNotFound are both acceptable
    m_initialized = (result == ConfigResult::Success ||
                     result == ConfigResult::FileNotFound);
    m_dirty = false;

    return m_initialized;
}

// =============================================================================
// Save and Reload
// =============================================================================

ConfigResult ConfigManager::Save() {
    if (!m_initialized) {
        return ConfigResult::IoError;
    }

    ConfigResult result = save_config(m_config_path, m_config);
    if (result == ConfigResult::Success) {
        m_dirty = false;
    }
    return result;
}

ConfigResult ConfigManager::Reload() {
    if (!m_initialized) {
        return ConfigResult::IoError;
    }

    // Reset to defaults first
    m_config = get_default_config();

    // Load from file
    ConfigResult result = load_config(m_config_path, m_config);

    // Clear dirty flag on successful reload
    if (result == ConfigResult::Success || result == ConfigResult::FileNotFound) {
        m_dirty = false;
    }

    return result;
}

// =============================================================================
// Server Settings
// =============================================================================

void ConfigManager::SetServerHost(const char* host) {
    if (host == nullptr) return;

    std::strncpy(m_config.server.host, host, MAX_HOST_LENGTH);
    m_config.server.host[MAX_HOST_LENGTH] = '\0';
    m_dirty = true;
    NotifyChange("server");
}

void ConfigManager::SetServerPort(uint16_t port) {
    m_config.server.port = port;
    m_dirty = true;
    NotifyChange("server");
}

void ConfigManager::SetUseTls(bool enabled) {
    m_config.server.use_tls = enabled;
    m_dirty = true;
    NotifyChange("server");
}

// =============================================================================
// Network Settings
// =============================================================================

void ConfigManager::SetConnectTimeout(uint32_t timeout_ms) {
    m_config.network.connect_timeout_ms = timeout_ms;
    m_dirty = true;
    NotifyChange("network");
}

void ConfigManager::SetPingInterval(uint32_t interval_ms) {
    m_config.network.ping_interval_ms = interval_ms;
    m_dirty = true;
    NotifyChange("network");
}

void ConfigManager::SetReconnectDelay(uint32_t delay_ms) {
    m_config.network.reconnect_delay_ms = delay_ms;
    m_dirty = true;
    NotifyChange("network");
}

void ConfigManager::SetMaxReconnectAttempts(uint32_t attempts) {
    m_config.network.max_reconnect_attempts = attempts;
    m_dirty = true;
    NotifyChange("network");
}

// =============================================================================
// LDN Settings
// =============================================================================

void ConfigManager::SetLdnEnabled(bool enabled) {
    m_config.ldn.enabled = enabled;
    m_dirty = true;
    NotifyChange("ldn");
}

void ConfigManager::SetPassphrase(const char* passphrase) {
    if (passphrase == nullptr) {
        m_config.ldn.passphrase[0] = '\0';
    } else {
        std::strncpy(m_config.ldn.passphrase, passphrase, MAX_PASSPHRASE_LENGTH);
        m_config.ldn.passphrase[MAX_PASSPHRASE_LENGTH] = '\0';
    }
    m_dirty = true;
    NotifyChange("ldn");
}

void ConfigManager::SetInterfaceName(const char* name) {
    if (name == nullptr) {
        m_config.ldn.interface_name[0] = '\0';
    } else {
        std::strncpy(m_config.ldn.interface_name, name, MAX_INTERFACE_LENGTH);
        m_config.ldn.interface_name[MAX_INTERFACE_LENGTH] = '\0';
    }
    m_dirty = true;
    NotifyChange("ldn");
}

// =============================================================================
// Debug Settings
// =============================================================================

void ConfigManager::SetDebugEnabled(bool enabled) {
    m_config.debug.enabled = enabled;
    m_dirty = true;
    NotifyChange("debug");
}

void ConfigManager::SetDebugLevel(uint32_t level) {
    // Clamp to valid range (0-3)
    m_config.debug.level = (level > 3) ? 3 : level;
    m_dirty = true;
    NotifyChange("debug");
}

void ConfigManager::SetLogToFile(bool enabled) {
    m_config.debug.log_to_file = enabled;
    m_dirty = true;
    NotifyChange("debug");
}

// =============================================================================
// Change Notification
// =============================================================================

void ConfigManager::SetChangeCallback(ConfigChangeCallback callback) {
    m_callback = callback;
}

void ConfigManager::NotifyChange(const char* section) {
    if (m_callback != nullptr) {
        m_callback(section);
    }
}

} // namespace ryu_ldn::config

/**
 * @file config_manager.hpp
 * @brief Global Configuration Manager for runtime config access
 *
 * Provides a singleton-style interface for accessing and modifying
 * configuration at runtime. Changes can be saved to disk and applied
 * without requiring a reboot.
 *
 * ## Thread Safety
 * All operations are thread-safe using a mutex.
 *
 * ## Usage
 * @code
 * #include "config/config_manager.hpp"
 *
 * // Initialize once at startup
 * ryu_ldn::config::ConfigManager::Instance().Initialize();
 *
 * // Read config
 * auto& cfg = ryu_ldn::config::ConfigManager::Instance().GetConfig();
 * printf("Server: %s\n", cfg.server.host);
 *
 * // Modify and save
 * ryu_ldn::config::ConfigManager::Instance().SetServerHost("example.com");
 * ryu_ldn::config::ConfigManager::Instance().Save();
 * @endcode
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include "config.hpp"
#include <cstdint>

namespace ryu_ldn::config {

/**
 * @brief Callback type for configuration change notifications
 *
 * @param section Changed section ("server", "network", "ldn", "debug")
 */
using ConfigChangeCallback = void (*)(const char* section);

/**
 * @brief Global configuration manager
 *
 * Singleton that manages runtime configuration with thread-safe access.
 */
class ConfigManager {
public:
    /**
     * @brief Get the singleton instance
     */
    static ConfigManager& Instance();

    /**
     * @brief Initialize the config manager
     *
     * Loads configuration from disk. Should be called once at startup
     * after filesystem is available.
     *
     * @param config_path Path to config file (default: CONFIG_PATH)
     * @return true if config loaded successfully
     */
    bool Initialize(const char* config_path = CONFIG_PATH);

    /**
     * @brief Check if initialized
     */
    bool IsInitialized() const { return m_initialized; }

    /**
     * @brief Get current configuration (read-only)
     */
    const Config& GetConfig() const { return m_config; }

    /**
     * @brief Save current configuration to disk
     *
     * @return ConfigResult indicating success or failure
     */
    ConfigResult Save();

    /**
     * @brief Reload configuration from disk
     *
     * Discards any unsaved changes.
     *
     * @return ConfigResult indicating success or failure
     */
    ConfigResult Reload();

    // =========================================================================
    // Server Settings
    // =========================================================================

    /**
     * @brief Get server host
     */
    const char* GetServerHost() const { return m_config.server.host; }

    /**
     * @brief Set server host
     *
     * @param host New hostname (max 128 chars)
     */
    void SetServerHost(const char* host);

    /**
     * @brief Get server port
     */
    uint16_t GetServerPort() const { return m_config.server.port; }

    /**
     * @brief Set server port
     */
    void SetServerPort(uint16_t port);

    /**
     * @brief Get TLS enabled state
     */
    bool GetUseTls() const { return m_config.server.use_tls; }

    /**
     * @brief Set TLS enabled state
     */
    void SetUseTls(bool enabled);

    // =========================================================================
    // Network Settings
    // =========================================================================

    /**
     * @brief Get connection timeout (ms)
     */
    uint32_t GetConnectTimeout() const { return m_config.network.connect_timeout_ms; }

    /**
     * @brief Set connection timeout (ms)
     */
    void SetConnectTimeout(uint32_t timeout_ms);

    /**
     * @brief Get ping interval (ms)
     */
    uint32_t GetPingInterval() const { return m_config.network.ping_interval_ms; }

    /**
     * @brief Set ping interval (ms)
     */
    void SetPingInterval(uint32_t interval_ms);

    /**
     * @brief Get reconnect delay (ms)
     */
    uint32_t GetReconnectDelay() const { return m_config.network.reconnect_delay_ms; }

    /**
     * @brief Set reconnect delay (ms)
     */
    void SetReconnectDelay(uint32_t delay_ms);

    /**
     * @brief Get max reconnect attempts
     */
    uint32_t GetMaxReconnectAttempts() const { return m_config.network.max_reconnect_attempts; }

    /**
     * @brief Set max reconnect attempts (0 = infinite)
     */
    void SetMaxReconnectAttempts(uint32_t attempts);

    // =========================================================================
    // LDN Settings
    // =========================================================================

    /**
     * @brief Get LDN enabled state
     */
    bool GetLdnEnabled() const { return m_config.ldn.enabled; }

    /**
     * @brief Set LDN enabled state
     */
    void SetLdnEnabled(bool enabled);

    /**
     * @brief Get passphrase
     */
    const char* GetPassphrase() const { return m_config.ldn.passphrase; }

    /**
     * @brief Set passphrase
     *
     * @param passphrase New passphrase (max 64 chars, empty = no passphrase)
     */
    void SetPassphrase(const char* passphrase);

    /**
     * @brief Get network interface name
     */
    const char* GetInterfaceName() const { return m_config.ldn.interface_name; }

    /**
     * @brief Set network interface name
     *
     * @param name Interface name (empty = auto-detect)
     */
    void SetInterfaceName(const char* name);

    // =========================================================================
    // Debug Settings
    // =========================================================================

    /**
     * @brief Get debug enabled state
     */
    bool GetDebugEnabled() const { return m_config.debug.enabled; }

    /**
     * @brief Set debug enabled state
     */
    void SetDebugEnabled(bool enabled);

    /**
     * @brief Get debug log level
     */
    uint32_t GetDebugLevel() const { return m_config.debug.level; }

    /**
     * @brief Set debug log level (0-3)
     */
    void SetDebugLevel(uint32_t level);

    /**
     * @brief Get log to file state
     */
    bool GetLogToFile() const { return m_config.debug.log_to_file; }

    /**
     * @brief Set log to file state
     */
    void SetLogToFile(bool enabled);

    // =========================================================================
    // Change Notification
    // =========================================================================

    /**
     * @brief Set callback for configuration changes
     *
     * @param callback Function to call when config changes (nullptr to clear)
     */
    void SetChangeCallback(ConfigChangeCallback callback);

    /**
     * @brief Check if config has unsaved changes
     */
    bool HasUnsavedChanges() const { return m_dirty; }

private:
    ConfigManager() = default;
    ~ConfigManager() = default;

    // Non-copyable
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    void NotifyChange(const char* section);

    Config m_config{};
    char m_config_path[256]{};
    bool m_initialized = false;
    bool m_dirty = false;
    ConfigChangeCallback m_callback = nullptr;
};

} // namespace ryu_ldn::config

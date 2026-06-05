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
 * @brief Validate passphrase format
 *
 * Passphrase must match the regex: Ryujinx-[0-9a-f]{8}
 * Empty passphrase (nullptr or "") is also valid (no filtering).
 *
 * @param passphrase Passphrase to validate
 * @return true if valid or empty, false otherwise
 */
/// @gdb{tag="CONFIG:MGR", msg="Validating passphrase"}
bool IsValidPassphrase(const char* passphrase);

/**
 * @brief Generate a random passphrase
 *
 * Generates a passphrase in format: Ryujinx-[0-9a-f]{8}
 * Uses random hex digits for the 8-character suffix.
 *
 * @param out Output buffer (at least 17 bytes for null terminator)
 * @param out_size Size of output buffer
 */
/// @gdb{tag="CONFIG:MGR", msg="Generating random passphrase"}
void GenerateRandomPassphrase(char* out, size_t out_size);

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
    /// @gdb{tag="CONFIG:MGR", msg="Getting manager instance"}
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
    /// @gdb{tag="CONFIG:MGR", msg="Initializing configuration manager"}
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
    /// @gdb{tag="CONFIG:MGR", msg="Saving configuration to disk"}
    ConfigResult Save();

    /**
     * @brief Reload configuration from disk
     *
     * Discards any unsaved changes.
     *
     * @return ConfigResult indicating success or failure
     */
    /// @gdb{tag="CONFIG:MGR", msg="Reloading configuration from disk"}
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
    /// @gdb{tag="CONFIG:MGR", msg="Setting server host"}
    void SetServerHost(const char* host);

    /**
     * @brief Get server port
     */
    uint16_t GetServerPort() const { return m_config.server.port; }

    /**
     * @brief Set server port
     */
    /// @gdb{tag="CONFIG:MGR", msg="Setting server port"}
    void SetServerPort(uint16_t port);


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
    /// @gdb{tag="CONFIG:MGR", msg="Setting LDN enabled"}
    void SetLdnEnabled(bool enabled);

    /**
     * @brief Get passphrase
     */
    const char* GetPassphrase() const { return m_config.ldn.passphrase; }

    /**
     * @brief Set passphrase
     *
     * Passphrase must match format: Ryujinx-[0-9a-f]{8}
     * Empty or nullptr clears the passphrase (allowed).
     *
     * @param passphrase New passphrase
     * @return true if set successfully, false if invalid format
     */
    /// @gdb{tag="CONFIG:MGR", msg="Setting passphrase"}
    bool SetPassphrase(const char* passphrase);

    /**
     * @brief Get use_passphrase state
     */
    bool GetUsePassphrase() const { return m_config.ldn.use_passphrase; }

    /**
     * @brief Set use_passphrase state
     *
     * When true, LDN rooms are filtered by passphrase. When false,
     * the sysmodule connects to public rooms regardless of passphrase.
     *
     * @param enabled 1 to enable passphrase filtering, 0 for public
     */
    /// @gdb{tag="CONFIG:MGR", msg="Setting use_passphrase"}
    void SetUsePassphrase(bool enabled);


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
    /// @gdb{tag="CONFIG:MGR", msg="Setting debug enabled"}
    void SetDebugEnabled(bool enabled);

    /**
     * @brief Get debug log level
     */
    uint32_t GetDebugLevel() const { return m_config.debug.level; }

    /**
     * @brief Set debug log level (0-3)
     */
    /// @gdb{tag="CONFIG:MGR", msg="Setting debug level"}
    void SetDebugLevel(uint32_t level);


    // =========================================================================
    // Change Notification
    // =========================================================================

    /**
     * @brief Set callback for configuration changes
     *
     * @param callback Function to call when config changes (nullptr to clear)
     */
    /// @gdb{tag="CONFIG:MGR", msg="Setting change callback"}
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

    /// @gdb{tag="CONFIG:MGR", msg="Config change notified"}
    void NotifyChange(const char* section);

    Config m_config{};
    char m_config_path[256]{};
    bool m_initialized = false;
    bool m_dirty = false;
    ConfigChangeCallback m_callback = nullptr;
};

} // namespace ryu_ldn::config

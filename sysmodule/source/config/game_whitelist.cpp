/**
 * @file game_whitelist.cpp
 * @brief LDN Game Whitelist
 *
 * Loads the whitelist once at startup and stores in a static array.
 * No file operations during MITM ShouldMitm calls.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "game_whitelist.hpp"
#include "../debug/log.hpp"

#include <stratosphere.hpp>
#include <cstring>
#include <new>

namespace ryu_ldn::config {

namespace {

constexpr const char* WHITELIST_PATH = "sdmc:/config/ryu_ldn_nx/gamelist.txt";

// Average bytes per line in gamelist.txt (e.g., "0x0100152000022000\n" = ~19 bytes)
constexpr size_t BYTES_PER_ENTRY = 18;

// Dynamically allocated whitelist (sized based on file)
/**
 * @brief Heap-allocated array holding the parsed whitelist of program IDs.
 *
 * @role Backing storage for the whitelist consulted by `IsGameWhitelisted` so
 *       the MITM ShouldMitm path can decide whether to intercept a game
 *       without touching the SD card on every IPC.
 * @modified_by `LoadWhitelist` (this file) — allocated via `new (std::nothrow)`
 *              once at startup, populated from `sdmc:/config/ryu_ldn_nx/gamelist.txt`,
 *              and never freed (sysmodule lifetime).
 * @thread_safety Not protected by a mutex. `LoadWhitelist` is invoked once during
 *                `InitializeSystemModule` before any MITM session can call
 *                `IsGameWhitelisted`; the load-then-freeze pattern means later
 *                read-only access from MITM threads is safe by happens-before.
 */
static u64* g_whitelist = nullptr;

/**
 * @brief Capacity (in u64 entries) of the g_whitelist array.
 *
 * @role Bounding size used by `LoadWhitelist` to stop parsing once the array
 *       is full and by the loader's loop guards.
 * @modified_by `LoadWhitelist` only — set during allocation, never mutated
 *              afterwards.
 * @thread_safety Same load-then-freeze discipline as g_whitelist; read-only
 *                after initialization.
 */
static size_t g_whitelist_capacity = 0;

/**
 * @brief Number of valid entries currently stored in g_whitelist.
 *
 * @role Active length of the whitelist; `IsGameWhitelisted` scans indices
 *       `[0, g_whitelist_count)` to test membership.
 * @modified_by `LoadWhitelist` only — incremented as each program ID is
 *              parsed from the file, then frozen.
 * @thread_safety Read-only after the one-shot `LoadWhitelist` run; no mutex
 *                needed by the load-then-freeze contract.
 */
static size_t g_whitelist_count = 0;

/**
 * @brief One-shot "initialized" flag preventing double-loads of the whitelist.
 *
 * @role Guarded latch so repeated `LoadWhitelist` calls (e.g. via IPC reconnect
 *       paths) short-circuit after the first successful or failed load.
 * @modified_by `LoadWhitelist` only — set to true on success and also on the
 *              empty-file / allocation-failure paths so the sysmodule still
 *              functions with an empty whitelist.
 * @thread_safety Read-only after the first `LoadWhitelist`; checked at the top
 *                of `LoadWhitelist` under the same load-then-freeze discipline.
 */
static bool g_whitelist_loaded = false;

/**
 * @brief Parse a hex string like "0x0100152000022000" to u64
 */
u64 ParseHexId(const char* str, size_t len) {
    if (!str || len == 0) return 0;

    size_t i = 0;

    // Skip "0x" or "0X" prefix if present
    if (len >= 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        i = 2;
    }

    u64 result = 0;
    while (i < len) {
        char c = str[i++];
        u64 digit;

        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = 10 + (c - 'a');
        } else if (c >= 'A' && c <= 'F') {
            digit = 10 + (c - 'A');
        } else {
            break;  // Invalid character, stop
        }

        result = (result << 4) | digit;
    }

    return result;
}

}  // namespace

void LoadWhitelist() {
    if (g_whitelist_loaded) {
        LOG_INFO("GameWhitelist: already loaded (%zu games)", g_whitelist_count);
        return;
    }

    LOG_INFO("GameWhitelist: loading from %s", WHITELIST_PATH);

    // Open the file
    ams::fs::FileHandle file;
    ams::Result rc = ams::fs::OpenFile(std::addressof(file), WHITELIST_PATH, ams::fs::OpenMode_Read);

    if (R_FAILED(rc)) {
        LOG_WARN("GameWhitelist: Cannot open %s (rc=0x%x)", WHITELIST_PATH, rc.GetValue());
        g_whitelist_loaded = true;  // Mark as loaded (empty)
        return;
    }

    // Get file size
    s64 file_size = 0;
    rc = ams::fs::GetFileSize(std::addressof(file_size), file);
    if (R_FAILED(rc) || file_size <= 0) {
        ams::fs::CloseFile(file);
        LOG_WARN("GameWhitelist: file empty or error");
        g_whitelist_loaded = true;
        return;
    }

    LOG_INFO("GameWhitelist: file_size=%lld bytes", file_size);

    // Calculate capacity based on file size and allocate
    g_whitelist_capacity = static_cast<size_t>(file_size / BYTES_PER_ENTRY) + 100;  // +100 margin
    g_whitelist = new (std::nothrow) u64[g_whitelist_capacity];
    if (!g_whitelist) {
        ams::fs::CloseFile(file);
        LOG_ERROR("GameWhitelist: failed to allocate %zu entries", g_whitelist_capacity);
        g_whitelist_loaded = true;
        return;
    }
    LOG_INFO("GameWhitelist: allocated %zu entries", g_whitelist_capacity);

    // Read file in chunks and parse lines
    constexpr size_t CHUNK_SIZE = 512;
    constexpr size_t LINE_BUF_SIZE = 24;
    char chunk[CHUNK_SIZE];
    char line_buf[LINE_BUF_SIZE];
    size_t line_len = 0;
    s64 offset = 0;

    while (offset < file_size && g_whitelist_count < g_whitelist_capacity) {
        size_t to_read = static_cast<size_t>(
            (file_size - offset) < static_cast<s64>(CHUNK_SIZE)
            ? (file_size - offset)
            : static_cast<s64>(CHUNK_SIZE)
        );

        size_t bytes_read = 0;
        rc = ams::fs::ReadFile(std::addressof(bytes_read), file, offset, chunk, to_read);
        if (R_FAILED(rc)) {
            LOG_WARN("GameWhitelist: ReadFile failed rc=0x%x", rc.GetValue());
            break;
        }

        // Process chunk byte by byte
        for (size_t i = 0; i < bytes_read && g_whitelist_count < g_whitelist_capacity; i++) {
            char c = chunk[i];

            if (c == '\n' || c == '\r') {
                // End of line - parse and add to whitelist
                if (line_len > 0) {
                    u64 id = ParseHexId(line_buf, line_len);
                    if (id != 0) {
                        g_whitelist[g_whitelist_count++] = id;
                    }
                    line_len = 0;
                }
            } else if (line_len < LINE_BUF_SIZE - 1) {
                line_buf[line_len++] = c;
            }
        }

        offset += bytes_read;
    }

    // Check last line if no trailing newline
    if (line_len > 0 && g_whitelist_count < g_whitelist_capacity) {
        u64 id = ParseHexId(line_buf, line_len);
        if (id != 0) {
            g_whitelist[g_whitelist_count++] = id;
        }
    }

    ams::fs::CloseFile(file);
    g_whitelist_loaded = true;

    LOG_INFO("GameWhitelist: loaded %zu games", g_whitelist_count);
}

bool IsGameInWhitelist(u64 program_id) {
    if (!g_whitelist_loaded) {
        LOG_WARN("GameWhitelist: not loaded yet!");
        return false;
    }

    // Simple linear search (fast enough for ~5000 entries)
    for (size_t i = 0; i < g_whitelist_count; i++) {
        if (g_whitelist[i] == program_id) {
            return true;
        }
    }

    return false;
}

}  // namespace ryu_ldn::config

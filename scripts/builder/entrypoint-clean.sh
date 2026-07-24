#!/usr/bin/env bash
################################################################################
# entrypoint-clean.sh — Clean all build artifacts
################################################################################
#
# PURPOSE:
#   Replaces the inline `command:` of the `clean` service in docker-compose.yml.
#   Cleans the sysmodule, overlay, tests, and output/ — and additionally cleans
#   the locally patched Atmosphere-libs build artifacts when the override is
#   active, so the next `build` recompiles libstratosphere from source and
#   picks up any patches.
#
# OVERRIDE DETECTION:
#   The override (docker-compose.override.yml) mounts the host's
#   ./sysmodule/Atmosphere-libs over /opt/ryu_ldn_nx. Both the .:/workspace
#   mount and the override mount expose the SAME host directory, so inside the
#   container /workspace/sysmodule/Atmosphere-libs and /opt/ryu_ldn_nx share
#   the same device + inode. The `-ef` test detects this reliably.
#   Without the override, /opt/ryu_ldn_nx is the image's overlay-fs layer
#   (different inode) → -ef is false → extended clean is skipped (nothing to
#   clean: the image's pre-built .a is immutable).
#
# IDempotence:
#   Every step is safe to re-run. `make clean` targets are guarded with
#   `|| true`; `rm -f` on absent files is a no-op.
#
################################################################################

set -euo pipefail

LOCAL_A="/workspace/sysmodule/Atmosphere-libs/libstratosphere/lib/nintendo_nx_arm64_armv8a/release/libstratosphere.a"
OVERRIDE_MOUNT="/opt/ryu_ldn_nx"
# ATMOSPHERE_LIBS_PATH: when set by docker-compose.override.yml, points to the
# local Atmosphere-libs checkout to use instead of the image's pre-built libs.
# Defaults to the standard workspace checkout path so the script works
# identically with or without the override.
LOCAL_LIBS_DIR="${ATMOSPHERE_LIBS_PATH:-/workspace/sysmodule/Atmosphere-libs}"

# ── Base clean: sysmodule, overlay, tests, output ─────────────────────────────
echo "[clean] 🧹 Cleaning sysmodule..."
cd /workspace/sysmodule && make clean 2>/dev/null || true

echo "[clean] 🧹 Cleaning overlay..."
cd /workspace/overlay && make clean 2>/dev/null || true

echo "[clean] 🧹 Cleaning tests..."
cd /workspace/tests && make clean 2>/dev/null || true

echo "[clean] 🧹 Removing output..."
rm -rf /workspace/output /workspace/ryu_ldn_nx-release.zip

# ── Extended clean: locally patched Atmosphere-libs ───────────────────────────
# Only when the override is active (local source mounted over /opt/ryu_ldn_nx).
# Without the override there is nothing to clean — the image's pre-built .a is
# immutable and the workspace checkout was never built locally.
if [ "$LOCAL_LIBS_DIR" -ef "$OVERRIDE_MOUNT" ]; then
    echo "[clean] 🧹 Override active — cleaning local libstratosphere build..."
    make -C "$LOCAL_LIBS_DIR/libstratosphere" clean-nx_release 2>/dev/null || true
    # Belt-and-suspenders: remove the release .a explicitly in case the
    # Makefile clean target misses it (e.g. partial build state).
    rm -f "$LOCAL_A"
    echo "[clean] ✅ Local libstratosphere cleaned"
else
    # No override: just remove the symlink we may have created during build
    # so the workspace checkout stays pristine. The image's real .a under
    # /opt/ryu_ldn_nx is NOT touched (immutable image layer).
    if [ -L "$LOCAL_A" ]; then
        echo "[clean] 🧹 Removing stale libstratosphere.a symlink..."
        rm -f "$LOCAL_A"
    fi
    echo "[clean] (override not active — skipping local libstratosphere clean)"
fi

echo "[clean] ✅ Done"
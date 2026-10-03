#!/bin/bash
# clang-tidy runner for ryu_ldn_nx project source files
# Scans sysmodule/source/ (not Atmosphere-libs/) by parsing each translation
# unit with clang-tidy 21 (LLVM 21) targeting aarch64-none-elf.
#
# Why clang-tidy 21: Debian bookworm ships clang-tidy 14, which cannot parse
# the GCC 15 libstdc++ headers shipped by current devkitA64 (C++23 constexpr
# math builtins, <format>, ranges). The Docker image installs clang-tidy-21
# from apt.llvm.org and symlinks it as `clang-tidy`.
#
# Known non-user-code noise (not project defects, kept out of the report):
#   * `constexpr variable ... must be initialized by a constant expression`
#     in Atmosphere-libs spl_types.hpp / wec_wake_event.hpp: GCC accepts
#     static_cast of out-of-range values to unscoped C enums (libnx typedefs);
#     clang implements CWG1766 strictly as a hard error. Atmosphere builds
#     with GCC only — these headers are never compiled by clang for real.
#   * `__crc32b` undeclared: libnx intrinsics require -march=armv8a+crc.
set -euo pipefail

echo "[lint] Running clang-tidy on ryu_ldn_nx source files..."

# Build the compilation database from the sysmodule Makefile
cd /workspace/sysmodule

# We need the cross-compiler from devkitA64 in PATH
DEVKITA64=${DEVKITA64:-/opt/devkitpro/devkitA64}
DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export PATH="${DEVKITPRO}/tools/bin:${DEVKITA64}/bin:${PATH}"

# Prefer clang-tidy-21 when present (LLVM 21 from apt.llvm.org), fall back
# to whatever clang-tidy is installed.
if command -v clang-tidy-21 > /dev/null 2>&1; then
    TIDY=clang-tidy-21
else
    TIDY=clang-tidy
fi

# Symlink pre-built libstratosphere so includes resolve
LIB_DIR="/workspace/sysmodule/Atmosphere-libs/libstratosphere/lib/nintendo_nx_arm64_armv8a/release"
PREBUILT="/opt/ryu_ldn_nx/libstratosphere/lib/nintendo_nx_arm64_armv8a/release/libstratosphere.a"
if [ -f "$PREBUILT" ] && [ ! -f "$LIB_DIR/libstratosphere.a" ]; then
    mkdir -p "$LIB_DIR"
    ln -sf "$PREBUILT" "$LIB_DIR/libstratosphere.a"
fi

# Collect all .cpp files from sysmodule/source/ (exclude Atmosphere-libs)
SOURCES_DIR="/workspace/sysmodule/source"
mapfile -t CPP_FILES < <(find "$SOURCES_DIR" -name "*.cpp" ! -path "*/Atmosphere-libs/*" | sort)

# Detect the devkitA64 GCC version (headers live under c++/<version>)
GCC_CXX_DIR="${DEVKITA64}/aarch64-none-elf/include/c++"
GCC_VER=""
if [ -d "$GCC_CXX_DIR" ]; then
    for ver in "$GCC_CXX_DIR"/*; do
        [ -d "$ver" ] || continue
        GCC_VER=$(basename "$ver")
        break
    done
fi
if [ -z "$GCC_VER" ]; then
    echo "[lint] ERROR: cannot detect devkitA64 C++ headers in $GCC_CXX_DIR" >&2
    exit 1
fi

# Build include flags matching the sysmodule Makefile setup.
# Order matters: the devkitA64 C++ headers must come BEFORE the newlib root
# include, otherwise #include_next <stdlib.h> from c++/<ver>/cstdlib fails
# to resolve. Non-user code paths use -isystem so clang-tidy treats headers
# found there as system headers (its warnings there are auto-suppressed).
INCLUDE_FLAGS=(
    "-I$SOURCES_DIR"
    "-I$SOURCES_DIR/config"
    "-I$SOURCES_DIR/debug"
    "-I$SOURCES_DIR/network"
    "-I$SOURCES_DIR/protocol"
    "-I$SOURCES_DIR/ldn"
    "-I$SOURCES_DIR/bsd"
    "-I$SOURCES_DIR/p2p"
    "-isystem /workspace/sysmodule/Atmosphere-libs/libstratosphere/include"
    "-isystem /workspace/sysmodule/Atmosphere-libs/libvapours/include"
    "-isystem /opt/ryu_ldn_nx/libstratosphere/include"
    "-isystem /opt/ryu_ldn_nx/libvapours/include"
    "-isystem ${DEVKITPRO}/libnx/include"
    "-isystem ${DEVKITPRO}/portlibs/switch/include"
    "-isystem ${DEVKITA64}/aarch64-none-elf/include/c++/${GCC_VER}"
    "-isystem ${DEVKITA64}/aarch64-none-elf/include/c++/${GCC_VER}/aarch64-none-elf"
    "-isystem ${DEVKITA64}/aarch64-none-elf/include"
    # Atmosphere build defines (from Atmosphere-libs/config: common.mk,
    # arch/arm64, arch/armv8a, board/nintendo/nx, os/horizon) — required for
    # vapours/sdmmc build config dispatch and svc architecture selection.
    "-DATMOSPHERE"
    "-DATMOSPHERE_ARCH_ARM64"
    "-DATMOSPHERE_ARCH_ARM_V8A"
    "-DATMOSPHERE_BOARD_NINTENDO_NX"
    "-DATMOSPHERE_OS_HORIZON"
    "-DATMOSPHERE_IS_STRATOSPHERE"
    "-D_GNU_SOURCE"
    "-D__SWITCH__"
    # Parse with clang targeting the devkitA64 triple so host /usr/include
    # headers are not picked up (aarch64-none-elf = bare-metal newlib target).
    "--target=aarch64-none-elf"
    # Match the sysmodule build language level (ATMOSPHERE_CXXFLAGS).
    "-std=gnu++23"
    # libnx crc.h uses ARM CRC intrinsics unconditionally; enable the
    # ISA features on the parsing target so they resolve.
    "-march=armv8a+crc"
)

# Build compile_commands.json for clang-tidy
echo "[lint] Generating compile_commands.json..."
COMPILE_DB="/workspace/sysmodule/build/compile_commands.json"
mkdir -p /workspace/sysmodule/build

echo "[" > "$COMPILE_DB"
first=true
for f in "${CPP_FILES[@]}"; do
    if [ "$first" = true ]; then
        first=false
    else
        echo "," >> "$COMPILE_DB"
    fi
    printf '  {\n    "directory": "/workspace/sysmodule",\n    "command": "clang++ %s -c %s",\n    "file": "%s"\n  }' \
        "${INCLUDE_FLAGS[*]}" "$f" "$f" >> "$COMPILE_DB"
done
echo "]" >> "$COMPILE_DB"

# Count files
NUM_FILES=${#CPP_FILES[@]}
echo "[lint] Found $NUM_FILES source files to check"

# Run clang-tidy with the compilation database
# -p points to the build directory containing compile_commands.json
# We use a subset of checks appropriate for embedded/system C++
CHECKS="
    -*,clang-analyzer-*,
    bugprone-*,
    -bugprone-easily-swappable-parameters,
    -bugprone-implicit-widening-of-multiplication-result,
    -bugprone-narrowing-conversions,
    -bugprone-reserved-identifier,
    misc-*,
    -misc-const-correctness,
    -misc-include-cleaner,
    -misc-non-private-member-variables-in-classes,
    -misc-no-recursion,
    -misc-unused-parameters,
    -misc-use-anonymous-namespace,
    -misc-use-internal-linkage,
    modernize-*,
    -modernize-avoid-c-arrays,
    -modernize-use-trailing-return-type,
    -modernize-macro-to-enum,
    -modernize-use-integer-sign-comparison,
    -modernize-use-std-print,
    performance-*,
    -performance-enum-size,
    -performance-avoid-endl,
    portability-*,
    readability-*,
    -readability-function-cognitive-complexity,
    -readability-identifier-length,
    -readability-identifier-naming,
    -readability-magic-numbers,
    -readability-redundant-access-specifiers,
    -readability-use-anyofallof
"
# Collapse whitespace
CHECKS=$(echo "$CHECKS" | tr -d '\n' | tr -s ',')

mkdir -p /workspace/build-logs

# Non-user-code hard errors to filter from the report (see header comment):
# spl_types / wec_wake_event (enum static_cast CWG1766) and crc.h intrinsics.
NOISE_PATTERN='(spl_types\.hpp|wec_wake_event[^:]*\.hpp|crc\.h):[0-9]+:[0-9]+: error:'

echo "[lint] Running $TIDY (checks enabled, this may take a minute)..."
# Run on all source files at once. Non-user-code hard errors (see header
# comment) are filtered from the report; they do not affect user-code
# warnings, which clang-tidy still emits.
"$TIDY" -p=/workspace/sysmodule/build \
    --checks="$CHECKS" \
    "${CPP_FILES[@]}" 2>&1 \
    | grep -v -E "${NOISE_PATTERN}|__crc32|Error while processing" \
    | tee /workspace/build-logs/clang-tidy.log || true

# Re-run the raw exit code check: count user-code findings in the log
WARN_COUNT=$(grep -c "warning:" /workspace/build-logs/clang-tidy.log || true)
ERR_COUNT=$(grep -c "error:" /workspace/build-logs/clang-tidy.log || true)
if [ "$ERR_COUNT" -gt 0 ]; then
    echo "[lint] ❌ clang-tidy found $ERR_COUNT parse error(s) in user code — see /workspace/build-logs/clang-tidy.log"
    exit 1
elif [ "$WARN_COUNT" -gt 0 ]; then
    echo "[lint] ❌ clang-tidy found $WARN_COUNT warning(s) — see /workspace/build-logs/clang-tidy.log"
    exit 1
else
    echo "[lint] ✅ clang-tidy passed with no findings in project source"
    exit 0
fi
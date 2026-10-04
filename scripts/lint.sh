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

# Probe which -std the installed clang-tidy accepts for parsing. Some
# clang-tidy builds reject gnu++23 ("invalid value"); try the historical
# C++23 alias, then gnu++20. The first accepted standard is used for both
# compile_commands.json and the final clang-tidy run.
CXX_STD=""
if command -v "$TIDY" > /dev/null 2>&1; then
    PROBE_DIR=$(mktemp -d)
    printf 'int main(){}\n' > "$PROBE_DIR/dummy.cpp"
    for std in gnu++23 gnu++2b gnu++20; do
        # Keep one check enabled: with -*, old clang-tidy builds abort with
        # "Error: no checks enabled" before parsing, which masked the real
        # "invalid value" rejection and made the probe always report success.
        "$TIDY" --checks=-*,readability-else-after-return "$PROBE_DIR/dummy.cpp" -- -std=$std \
            > "$PROBE_DIR/out.txt" 2>&1 || true
        if ! grep -q "invalid value" "$PROBE_DIR/out.txt"; then
            CXX_STD=$std
            break
        fi
    done
    rm -f "$PROBE_DIR/dummy.cpp" "$PROBE_DIR/out.txt"
    rmdir "$PROBE_DIR"
fi
if [ -z "$CXX_STD" ]; then
    echo "[lint] WARNING: no -std accepted by $TIDY, defaulting to gnu++23" >&2
    CXX_STD=gnu++23
fi
echo "[lint] Using C++ standard: $CXX_STD"

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
    # $CXX_STD is probed at runtime above (gnu++23 >> gnu++2b >> gnu++20).
    "-std=$CXX_STD"
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
# Hardened check set for a boot2 sysmodule (10MB RAM budget, absolute
# stability): cppcoreguidelines-* + hicpp-* enforce memory safety and
# low-level correctness on top of the bugprone/performance baseline.
# Fixed-size arrays are the project's preferred allocation strategy —
# modernize-avoid-c-arrays stays disabled, and the special-memory
# / ownership rules that would flag the lmem heap overrides are scoped
# out rather than disabled wholesale.
CHECKS="
    -*,clang-analyzer-*,
    bugprone-*,
    -bugprone-easily-swappable-parameters,
    -bugprone-implicit-widening-of-multiplication-result,
    -bugprone-narrowing-conversions,
    -bugprone-reserved-identifier,
    clang-diagnostic-*,
    -clang-diagnostic-error,
    cppcoreguidelines-*,
    -cppcoreguidelines-avoid-c-arrays,
    # do-while is the natural shape of bare-metal retry loops (protocol
    # handshake, socket poll); Core Guideline ES.75 is a style preference,
    # not a memory-safety rule — disabled for this target.
    -cppcoreguidelines-avoid-do-while,
    -cppcoreguidelines-avoid-magic-numbers,
    # The sysmodule legitimately uses non-const globals: the lmem heap
    # (g_heap_memory), the shared-state bridge (LdnSharedState), and the
    # Atmosphere service registration globals. Scope: boot2 single-instance
    # process, globals are the project's documented pattern (AGENTS.md).
    -cppcoreguidelines-avoid-non-const-global-variables,
    -cppcoreguidelines-init-variables,
    -cppcoreguidelines-macro-usage,
    -cppcoreguidelines-narrowing-conversions,
    -cppcoreguidelines-non-private-member-variables-in-classes,
    -cppcoreguidelines-prefer-member-initializer,
    -cppcoreguidelines-slicing,
    -cppcoreguidelines-special-member-functions,
    -cppcoreguidelines-virtual-class-destructor,
    # Pointer arithmetic and constant-array-index are unavoidable in a
    # wire-format parser (packet_buffer.hpp) and BSD socket code; the
    # bounds are enforced by static_assert on struct sizes + data_size
    # validation, not by the type system.
    -cppcoreguidelines-pro-bounds-pointer-arithmetic,
    -cppcoreguidelines-pro-bounds-constant-array-index,
    # Fixed-size buffers passed as function arguments decay to pointers —
    # that is the documented project pattern (fixed buffers preferred over
    # std::vector for memory predictability, AGENTS.md Memory Constraints).
    -cppcoreguidelines-pro-bounds-array-to-pointer-decay,
    # Horizon IPC service calls (fsOpenFile, svcGetInfo, tipc/cmif
    # marshalling) and snprintf-based logging are C-style vararg APIs
    # mandated by libnx/Atmosphere; there is no typed alternative.
    -cppcoreguidelines-pro-type-vararg,
    # reinterpret_cast is required for IPC marshalling (PointerBuffers),
    # network byte-order shims, and the lmem heap overlays. Kept OUT of
    # the enabled set: 63 sites, all mandated by the Horizon/libnx API.
    -cppcoreguidelines-pro-type-reinterpret-cast,
    # dns_wrap.cpp implements __wrap_getaddrinfo/__wrap_freeaddrinfo: the
    # POSIX C ABI (malloc'd linked list freed by freeaddrinfo) is imposed
    # by the --wrap linker flags and consumed by miniupnpc (C code). A
    # container or smart pointer would break the ABI; the allocation pair
    # is symmetric and audited (codeql annotations in the file).
    -cppcoreguidelines-no-malloc,
    hicpp-*,
    # -hicpp-no-malloc mirrors the cppcoreguidelines exclusion above
    # (same check registered under both names).
    -hicpp-no-malloc,
    -hicpp-avoid-c-arrays,
    -hicpp-braced-list-init,
    -hicpp-deprecated-headers,
    -hicpp-ignored-remove-result,
    # hicpp-no-array-decay / hicpp-vararg / hicpp-member-init are aliases
    # of the cppcoreguidelines checks excluded above — keep the exclusion
    # sets in sync so the alias does not re-enable what the main check
    # disabled.
    -hicpp-no-array-decay,
    -hicpp-vararg,
    -hicpp-member-init,
    -hicpp-multiway-paths-with-side-effects,
    -hicpp-named-parameter,
    -hicpp-no-assembler,
    -hicpp-noarray-decay,
    -hicpp-signed-bitwise,
    -hicpp-special-member-functions,
    -hicpp-static-assert,
    -hicpp-use-auto,
    -hicpp-use-emplace,
    -hicpp-use-equals-default,
    -hicpp-use-equals-delete,
    -hicpp-use-nullptr,
    -hicpp-use-override,
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
# Collapse whitespace: strip comment lines first (they would otherwise be
# glued to the next token once newlines are removed and silently break the
# check list), then join, then collapse duplicate commas and stray spaces.
CHECKS=$(echo "$CHECKS" | grep -v '^\s*#' | tr -d '\n' | tr -s ' ,' ',')

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
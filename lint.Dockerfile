# Lint image for ryu_ldn_nx.
#
# Extends the published dev environment image with clang-tidy-21 from
# apt.llvm.org. Debian bookworm's stock clang-tidy 14 cannot parse the
# GCC 15 libstdc++ headers shipped by current devkitA64 (C++23 constexpr
# math builtins, <format>, ranges) and segfaults on them — see the header
# comment in scripts/lint.sh. The binary is symlinked as `clang-tidy` so
# scripts can invoke it without a version suffix.
#
# Usage:
#   docker compose build lint   (or: docker compose run --rm lint)
#
# Keep the base image reference in sync with docker-compose.yml.

ARG BASE_IMAGE=ghcr.io/ethiquema/ryu_ldn_nx:latest
FROM ${BASE_IMAGE}

RUN apt-get update && apt-get install -y --no-install-recommends gnupg && \
    rm -rf /var/lib/apt/lists/* && \
    curl -fsSL -o /tmp/llvm-snapshot.gpg.key https://apt.llvm.org/llvm-snapshot.gpg.key && \
    gpg --yes --batch --dearmor -o /usr/share/keyrings/llvm.gpg /tmp/llvm-snapshot.gpg.key && \
    rm -f /tmp/llvm-snapshot.gpg.key && \
    echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/bookworm/ llvm-toolchain-bookworm-21 main" \
        > /etc/apt/sources.list.d/llvm21.list && \
    apt-get update && \
    apt-get install -y --no-install-recommends clang-tidy-21 && \
    ln -sf /usr/bin/clang-tidy-21 /usr/local/bin/clang-tidy && \
    rm -rf /var/lib/apt/lists/* && \
    clang-tidy --version
# ryu_ldn_nx Development Container
# Based on devkitPro for Nintendo Switch development

FROM devkitpro/devkita64:latest

LABEL maintainer="ryu_ldn_nx contributors"
LABEL description="Development environment for ryu_ldn_nx Switch sysmodule"

# Set environment variables
ENV DEVKITPRO=/opt/devkitpro
ENV DEVKITARM=${DEVKITPRO}/devkitARM
ENV DEVKITA64=${DEVKITPRO}/devkitA64
ENV PATH=${DEVKITPRO}/tools/bin:${DEVKITA64}/bin:${PATH}

# Install additional system dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    git \
    curl \
    wget \
    zip \
    unzip \
    python3 \
    python3-pip \
    doxygen \
    graphviz \
    && rm -rf /var/lib/apt/lists/*

# Install Switch development libraries via dkp-pacman
RUN dkp-pacman -Syyu --noconfirm && \
    dkp-pacman -S --noconfirm \
    switch-dev \
    libnx \
    switch-curl \
    switch-zlib \
    switch-mbedtls \
    devkitA64-gdb

# Create non-root user (UID/GID will be updated by VSCode's updateRemoteUserUID)
RUN useradd -m -s /bin/bash devuser

# Create workspace directory
WORKDIR /workspace

# Set default shell to bash
SHELL ["/bin/bash", "-c"]

# Default command
CMD ["/bin/bash"]
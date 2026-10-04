# Root Makefile for ryu_ldn_nx
#
# Thin wrapper around the Docker Compose services defined in
# docker-compose.yml. All real work happens inside the dev container
# (ghcr.io/ethiquema/ryu_ldn_nx:monthly-20260701) so the host only needs
# Docker + docker compose — no devkitPro install required.
#
# Usage:
#   make build       # build sysmodule + overlay + dist ZIP
#   make test        # run host unit tests
#   make clean       # wipe build artifacts and output/
#   make dist        # package SD card layout + release ZIP
#   make lint        # run clang-tidy on project sources
#   make shell       # interactive shell inside the build container
#   make debug IP=... [PID=...]  # interactive GDB session
#
# See docker-compose.yml for the authoritative service definitions.
#
# DEBUG_HEX_DUMP is intentionally NOT defined by default — guards remove
# hex dumps from production builds.

SHELL := /bin/sh

# docker compose binary (v2). Override if your distro ships a different name.
DOCKER_COMPOSE ?= docker compose

# Optional args passed through to the debugger service.
IP  ?=
PID ?=

.PHONY: build test clean dist lint shell debug help

help: ## Show this help
	@awk 'BEGIN {FS = ":.*##"} \
	     /^[a-zA-Z_-]+:.*##/ {printf "  %-12s %s\n", $$1, $$2}' $(MAKEFILE_LIST)

build: ## Build sysmodule + overlay + dist ZIP (docker compose run --rm build)
	$(DOCKER_COMPOSE) run --rm build

test: ## Run host unit tests (docker compose run --rm test)
	$(DOCKER_COMPOSE) run --rm test

clean: ## Clean all build artifacts and output/ (docker compose run --rm clean)
	$(DOCKER_COMPOSE) run --rm clean

dist: ## Package SD card layout + release ZIP (runs the dist target in sysmodule)
	$(DOCKER_COMPOSE) run --rm build sh -c 'cd /workspace/sysmodule && make dist'

lint: ## Run clang-tidy on project sources (docker compose run --rm lint)
	$(DOCKER_COMPOSE) run --rm lint

shell: ## Interactive shell inside the build container (docker compose run --rm shell)
	$(DOCKER_COMPOSE) run --rm shell

debug: ## Interactive GDB session: make debug IP=<SWITCH_IP> [PID=<pid>]
	@if [ -z "$(IP)" ]; then \
		echo "Usage: make debug IP=<SWITCH_IP> [PID=<pid>]" >&2; \
		exit 2; \
	fi
	$(DOCKER_COMPOSE) run --rm debugger $(IP) $(PID)
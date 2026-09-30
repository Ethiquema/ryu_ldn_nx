#!/usr/bin/env bash
# check-submodules.sh — Detect outdated git submodules (bulk drift check).
#
# Structured enumeration only: NO grep/sed/awk textual parsing of .gitmodules.
# Submodules are enumerated via `git config --file .gitmodules --get-regexp`,
# yielding one `submodule.<path>.url` key per submodule. The command separates
# key and value with a single SPACE (not a tab) when reading `--file`, so the
# parse below splits on the first space only.
#
# Outputs (stdout):
#   - Human-readable log lines on stderr.
#   - A JSON array of outdated submodules as the LAST line (compact when jq
#     is available so it stays single-line for $GITHUB_OUTPUT):
#     [{"path":"...","url":"...","local_sha":"...","remote_sha":"..."}, ...]
#
# Outputs (GITHUB_OUTPUT, only when set):
#   - has_updates=true|false
#   - outdated=<JSON array>
#
# Exit codes:
#   0 = success (with or without updates)
#   1 = usage / environment error
#
# Usage:
#   bash scripts/ci/check-submodules.sh                    # from repo root
#   GITHUB_OUTPUT=/tmp/out bash scripts/ci/check-submodules.sh

set -euo pipefail

# ---------------------------------------------------------------------------
# Enumeration — structured, via git config (never textually parsed).
# ---------------------------------------------------------------------------
# Each line from --get-regexp is: `submodule.<path>.url <url>` (space-separated).
# Splitting once on the first space with ${var%% *} / ${var#* } is deterministic:
# config keys and URLs never contain spaces. Paths with spaces in .gitmodules
# are fine because the key here is the submodule NAME, not its path.
list_submodules() {
	local gitmodules="$1"
	local lines
	lines="$(git config --file "${gitmodules}" --get-regexp '^submodule\..*\.url$' || true)"

	[[ -n "${lines}" ]] || return 0

	local line key url path
	while IFS= read -r line; do
		key="${line%% *}"
		url="${line#* }"
		path="${key#submodule.}"
		path="${path%.url}"
		printf '%s\n%s\n' "${path}" "${url}"
	done <<< "${lines}"
}

# ---------------------------------------------------------------------------
# Drift comparison. Populates the global OUTDATED_ITEMS array with JSON
# fragments (one per outdated submodule). Returns 0 if any update exists.
# ---------------------------------------------------------------------------
check_all_submodules() {
	local repo_root="$1"
	OUTDATED_ITEMS=()
	local count=0

	local -a entries_array=()
	mapfile -t entries_array < <(list_submodules "${repo_root}/.gitmodules")

	if [[ ${#entries_array[@]} -eq 0 ]]; then
		echo "check-submodules: no submodules defined" >&2
		return 1
	fi

	local i
	for (( i = 0; i < ${#entries_array[@]}; i += 2 )); do
		local path="${entries_array[i]}"
		local url="${entries_array[i + 1]}"
		local sub_path="${repo_root}/${path}"

		if [[ ! -d "${sub_path}" ]]; then
			echo "check-submodules: WARNING '${path}' not initialized, skipping" >&2
			continue
		fi

		local ls_output local_sha remote_sha
		ls_output="$(git ls-remote "${url}" HEAD)"
		# ls-remote separates SHA and ref with a TAB, not a space.
		remote_sha="${ls_output%%$'\t'*}"
		local_sha="$(git -C "${sub_path}" rev-parse HEAD)"
		if [[ -z "${remote_sha}" ]]; then
			echo "check-submodules: WARNING no remote HEAD for '${path}' (${url}), skipping" >&2
			continue
		fi

		if [[ "${local_sha}" != "${remote_sha}" ]]; then
			OUTDATED_ITEMS+=("$(printf '{"path":"%s","url":"%s","local_sha":"%s","remote_sha":"%s"}' \
				"${path}" "${url}" "${local_sha}" "${remote_sha}")")
			echo "check-submodules: OUTDATED  ${path}  ${local_sha:0:7} -> ${remote_sha:0:7}" >&2
		else
			echo "check-submodules: up-to-date  ${path} (${local_sha:0:7})" >&2
		fi
		count=$((count + 1))
	done

	echo "check-submodules: checked ${count} submodule(s)" >&2
	[[ ${#OUTDATED_ITEMS[@]} -gt 0 ]]
}

# ---------------------------------------------------------------------------
# JSON report — jq when available, printf fallback so the script runs
# testable locally without jq installed.
# ---------------------------------------------------------------------------
build_json() {
	if [[ ${#OUTDATED_ITEMS[@]} -eq 0 ]]; then
		printf '[]'
		return 0
	fi

	if command -v jq >/dev/null 2>&1; then
		printf '%s\n' "${OUTDATED_ITEMS[@]}" | jq -s -c .
	else
		local joined
		joined="$(IFS=,; printf '%s' "${OUTDATED_ITEMS[*]}")"
		printf '[%s]' "${joined}"
	fi
}

main() {
	local repo_root
	repo_root="$(git rev-parse --show-toplevel)"

	if [[ ! -f "${repo_root}/.gitmodules" ]]; then
		echo "check-submodules: no .gitmodules in ${repo_root}" >&2
		return 1
	fi

	local has_updates=false outdated_json
	if check_all_submodules "${repo_root}"; then
		has_updates=true
	fi
	outdated_json="$(build_json)"

	echo "${outdated_json}"

	# GITHUB_OUTPUT only: local runs are not GitHub Actions.
	if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
		{
			echo "has_updates=${has_updates}"
			echo "outdated=${outdated_json}"
		} >> "${GITHUB_OUTPUT}"
	fi
}

main "$@"
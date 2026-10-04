#!/usr/bin/env bash
# bulk-update-submodules.sh — Bulk-update ALL submodules whose remote HEAD
# differs from the locally pinned commit. Companion of check-submodules.sh:
# this script TRUSTS the caller to have created the feature branch already;
# it only refreshes the working tree pointers, commits them (DCO `git commit -s`)
# and prints the updated submodule list.
#
# Steps:
#   1. Enumerate submodules (structured git-config enumeration — check-submodules.sh).
#   2. `git submodule update --remote` on every submodule whose local HEAD
#      differs from the remote HEAD.
#   3. Stage the gitlink changes and, if anything changed, commit with
#      `chore(deps): update submodules [names]` signed off via `git commit -s`.
#
# Outputs (stderr): human-readable log lines.
# Outputs (stdout): last line = comma-separated names of UPDATED submodules,
#                   or an empty line when nothing changed.
# Exit codes:
#   0 = success (with or without updates)
#
# Usage (from the repo root, on the feature branch):
#   ./scripts/ci/bulk-update-submodules.sh

set -euo pipefail

# ---------------------------------------------------------------------------
# Enumeration — same structured git-config enumeration as check-submodules.sh.
# Returns one line per submodule: `path<TAB>url`. Never textually parsed.
# ---------------------------------------------------------------------------
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
		printf '%s\t%s\n' "${path}" "${url}"
	done <<< "${lines}"
}

main() {
	local repo_root
	repo_root="$(git rev-parse --show-toplevel)"

	local -a entries_array=()
	mapfile -t entries_array < <(list_submodules "${repo_root}/.gitmodules")

	if [[ ${#entries_array[@]} -eq 0 ]]; then
		echo "bulk-update-submodules: no submodules defined" >&2
		printf '\n'
		return 0
	fi

	local -a paths=() names=()
	local i path url sub_path
	for (( i = 0; i < ${#entries_array[@]}; i += 1 )); do
		path="${entries_array[i]%%$'\t'*}"
		url="${entries_array[i]#*$'\t'}"
		sub_path="${repo_root}/${path}"

		[[ -d "${sub_path}" ]] || {
			echo "bulk-update-submodules: skipping '${path}' (not initialized)" >&2
			continue
		}

		# Cheap drift check against the submodule's own pinned remote.
		local remote_sha local_sha
		remote_sha="$(git ls-remote "${url}" HEAD)"
		remote_sha="${remote_sha%%$'\t'*}"
		local_sha="$(git -C "${sub_path}" rev-parse HEAD)"

		if [[ "${local_sha}" != "${remote_sha}" ]]; then
			paths+=("${path}")
			names+=("$(basename "${path}")")
		fi
	done

	if [[ ${#paths[@]} -eq 0 ]]; then
		echo "bulk-update-submodules: all submodules up-to-date" >&2
		printf '\n'
		return 0
	fi

	# Bulk update: bump every outdated submodule to its remote HEAD.
	git submodule update --remote "${paths[@]}"

	# Verify something actually changed before committing.
	if git diff --quiet; then
		echo "bulk-update-submodules: pointers unchanged ('--remote' resolved to same SHAs)" >&2
		printf '\n'
		return 0
	fi

	git add -- "${paths[@]}"

	local message
	message="$(printf 'chore(deps): update submodules [%s]' "$(IFS=,; printf '%s' "${names[*]}")")"
	echo "bulk-update-submodules: committing '${message}'" >&2
	git commit -s -m "${message}"

	IFS=, printf '%s\n' "${names[*]}"
}

main "$@"
#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail

top=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
deps=$top/deps
patches=$top/patches

declare -A patched_head=(
    [microkit]=146330229413048ac38170620b9e81a30a60594b
    [rust-sel4]=49fb1e9d35b9f04a71201383e6839c166c36c501
    [seL4]=c867f3a2734382460a78292251497f001d803de8
)

die() {
    echo "fetch-deps: $*" >&2
    exit 1
}

repo_init() {
    mkdir -p "$deps"
    (cd "$deps" && repo init -u "$top" -m manifest/default.xml)
}

check_clean() {
    local path
    for path in $(cd "$deps" && repo list -p); do
        [ -d "$deps/$path" ] || continue
        if [ -n "$(git -C "$deps/$path" status --porcelain)" ]; then
            die "deps/$path has uncommitted changes, refusing to sync"
        fi
    done
}

patch_author() {
    sed -n '/^From: /{s/^From: //p;q}' "$1"
}

apply_patch() {
    local project=$1 patch=$2 author
    author=$(patch_author "$patch")
    [ -n "$author" ] || die "no author in $patch"
    GIT_COMMITTER_NAME=${author% <*} \
    GIT_COMMITTER_EMAIL=$(echo "$author" | sed 's/.*<\(.*\)>/\1/') \
        git -C "$deps/$project" am --committer-date-is-author-date "$patch"
}

apply_patches() {
    local project=$1 patch head
    for patch in "$patches/$project"/*.patch; do
        apply_patch "$project" "$patch"
    done
    head=$(git -C "$deps/$project" rev-parse HEAD)
    if [ "$head" != "${patched_head[$project]}" ]; then
        die "deps/$project is at $head after patching, expected ${patched_head[$project]}"
    fi
}

main() {
    [ -d "$deps/.repo" ] || repo_init
    check_clean
    (cd "$deps" && repo sync -d)
    local project
    for project in "${!patched_head[@]}"; do
        apply_patches "$project"
    done
}

main "$@"

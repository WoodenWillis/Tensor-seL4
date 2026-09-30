#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail

top=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
deps=$top/deps
patches=$top/patches

declare -A patched_head=(
    [libvmm]=ae52d393e2aaa8a7fc9a75ef39f889e8c31250a2
    [microkit]=da7c8bf1b98839ae7e13637869fd44e59e05a38a
    [rust-sel4]=49fb1e9d35b9f04a71201383e6839c166c36c501
    [seL4]=c867f3a2734382460a78292251497f001d803de8
)

die() {
    echo "fetch-deps: $*" >&2
    exit 1
}

repo_init() {
    mkdir -p "$deps"
    (cd "$deps" && repo init --standalone-manifest -u "file://$top/manifest/default.xml")
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
    repo_init
    check_clean
    (cd "$deps" && repo sync -d)
    local project
    for project in "${!patched_head[@]}"; do
        apply_patches "$project"
    done
}

main "$@"

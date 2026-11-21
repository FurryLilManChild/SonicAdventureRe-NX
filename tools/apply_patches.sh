#!/bin/sh
# SonicAdventureRe-nx: applies patches/decomp/*.patch, in order, to the decomp submodule, once.
# A clean submodule gets them all. A changed one must be exactly the patches' result (checked in
# a scratch worktree with them applied), else this stops: a change outside the patches would be
# lost to the next checkout, so it belongs in a patch first.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
d="$root/decomp"
if [ -z "$(git -C "$d" status --porcelain --untracked-files=no)" ]; then
    for p in "$root"/patches/decomp/*.patch; do
        git -C "$d" apply --whitespace=nowarn "$p"
        echo "applied: $(basename "$p")"
    done
    exit 0
fi
w=$(mktemp -d)
git -C "$d" worktree add -q --detach "$w" HEAD
for p in "$root"/patches/decomp/*.patch; do
    git -C "$w" apply --whitespace=nowarn "$p"
done
differ=0
for f in $(git -C "$w" status --porcelain --untracked-files=all | awk '{print $2}') \
         $(git -C "$d" status --porcelain --untracked-files=no | awk '{print $2}'); do
    cmp -s "$w/$f" "$d/$f" || { echo "decomp/$f differs from the patches' result" >&2; differ=1; }
done
git -C "$d" worktree remove --force "$w"
if [ $differ = 1 ]; then
    echo "apply_patches: the decomp holds changes outside patches/decomp: make them a patch first" >&2
    exit 1
fi
echo "patches already applied: $(ls "$root"/patches/decomp/*.patch | wc -l) of them"

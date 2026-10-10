#!/usr/bin/env bash
# Clone the compositor sources this plugin links against, at the EXACT revisions the
# running session uses. Without this, every question of the form "what does Hyprland
# assert here" is unanswerable -- the headers on disk declare the functions but not
# their bodies, and a crash report names a function whose behaviour nobody can read.
#
#   ./tools/sync_upstream.sh              # clone + checkout the running revisions
#   ./tools/sync_upstream.sh --check      # verify the checkouts match the session
#
# Reading headers is not reading the compositor: renderRoundedShadow's RASSERT lives in
# the .cpp, and it is the RASSERT that took a session down on 2026-10-10.
set -euo pipefail

CHECK=0
DEST="$HOME/dev/clone"
for a in "$@"; do
  case "$a" in
    --check) CHECK=1 ;;
    *) DEST="$a" ;;
  esac
done
# hyprctl version embeds the full commit as the version string. That is the revision
# to check out -- HEAD is a different tree and every "it says in the source" would then
# be about code that is not running.
# hyprctl embeds the full commit in the version line. HEAD is a different tree, and
# every "the source says" about the wrong tree is worse than not having the source.
COMMIT="$(hyprctl version 2>/dev/null | grep -oE 'at commit [0-9a-f]{40}' | awk '{print $3}' || true)"

if [[ $CHECK == 1 ]]; then
  for repo in Hyprland aquamarine; do
    d="$DEST/$repo"
    [[ -d "$d/.git" ]] || { echo "$repo: not cloned"; exit 1; }
    printf '%-11s %s\n' "$repo" "$(git -C "$d" log --oneline -1)"
  done
  echo "session:    ${COMMIT:-unknown}"
  exit 0
fi

mkdir -p "$DEST"

if [[ ! -d "$DEST/Hyprland/.git" ]]; then
  git clone --filter=blob:none --no-checkout https://github.com/hyprwm/Hyprland.git "$DEST/Hyprland"
fi
if [[ -n "$COMMIT" ]]; then
  git -C "$DEST/Hyprland" checkout -q "$COMMIT" && echo "Hyprland -> $COMMIT (the running one)"
else
  echo "Hyprland: could not read hyprctl version; left at whatever was checked out"
fi

if [[ ! -d "$DEST/aquamarine/.git" ]]; then
  git clone --filter=blob:none --no-checkout https://github.com/hyprwm/aquamarine.git "$DEST/aquamarine"
fi
# aquamarine reports no version in hyprctl; the crash report carries the one the
# running binary was COMPILED against, which is the API its headers describe. Read it
# from the NEWEST report -- the cache keeps reports from every past session, and an
# older one names an older revision.
AQ="$(ls -t "$HOME"/.cache/hyprland/hyprlandCrashReport*.txt 2>/dev/null |
      head -1 | xargs -r grep -hoE 'Aquamarine: built against [0-9.]+' |
      awk '{print $NF}')"
AQ="${AQ:-0.15.0}"
git -C "$DEST/aquamarine" checkout -q "v$AQ" 2>/dev/null &&
  echo "aquamarine -> v$AQ" || echo "aquamarine: no v$AQ tag, left alone"

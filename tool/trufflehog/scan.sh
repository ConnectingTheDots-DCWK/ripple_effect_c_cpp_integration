#!/usr/bin/env bash
# The pre-commit hook's secret scan: TruffleHog over what is staged, failing
# on any finding. Finds the binary in the places it installs to, because a
# git started from a desktop editor has the desktop's PATH, not a shell's —
# mise's shims first, which run the version mise.toml pins, CI's.
shims="${MISE_DATA_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/mise}/shims"
[ -d "$shims" ] && PATH="$shims:$PATH"
for dir in "$HOME/.local/bin" /usr/local/bin /opt/homebrew/bin "$HOME/go/bin"; do
  command -v trufflehog >/dev/null 2>&1 && break
  [ -x "$dir/trufflehog" ] && PATH="$dir:$PATH"
done
if ! command -v trufflehog >/dev/null 2>&1; then
  echo "trufflehog is not installed, so this commit cannot be scanned for secrets." >&2
  echo "Install it (see .pre-commit-config.yaml), then commit again." >&2
  exit 1
fi
# TRUFFLEHOG_PRE_COMMIT makes it scan the staged change and fail on a find.
# --no-update because a scan otherwise replaces its own binary with the newest
# release, in place — which is how a pinned 3.99.0 became 3.99.3.
TRUFFLEHOG_PRE_COMMIT=1 exec trufflehog git file://. --no-update

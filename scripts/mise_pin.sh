#!/usr/bin/env bash
# A version from mise.toml, for a script or a workflow that needs one without
# needing mise — not every job installs it, and a consumer of this repository
# never does. The key is the tool's entry as mise.toml spells it.
#
#   scripts/mise_pin.sh aqua:Kitware/CMake
#     prints the pinned version
#   scripts/mise_pin.sh aqua:Kitware/CMake cmake
#     also runs `cmake --version` and fails, saying what to do, unless
#     the first version in its output is the pin
#
# Reads the shapes mise.toml uses, `"key" = "1.2.3"`, `key = "1.2.3"` and
# `"key" = { version = "1.2.3", … }`, and nothing cleverer: a pin written any
# other way is a failure here rather than a guess.
set -euo pipefail

key=${1:?usage: scripts/mise_pin.sh <mise.toml key> [command]}
command=${2:-}
manifest="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/mise.toml"

pin=$(awk -v key="$key" '
  { rest = "" }
  # `"aqua:x/y" = …` for a backend key, `python = …` for a bare one.
  index($0, "\"" key "\"") == 1 { rest = substr($0, length(key) + 3) }
  index($0, key) == 1 && substr($0, length(key) + 1) ~ /^ *=/ { rest = substr($0, length(key) + 1) }
  rest != "" {
    if (match(rest, /version *= *"[^"]+"/)) {
      rest = substr(rest, RSTART, RLENGTH); sub(/^version *= */, "", rest)
    } else if (match(rest, /^ *= *"[^"]+"/)) {
      rest = substr(rest, RSTART, RLENGTH); sub(/^ *= */, "", rest)
    } else next
    gsub(/"/, "", rest); print rest; exit
  }' "$manifest")

if [ -z "$pin" ]; then
  echo "mise_pin: no pin for \"$key\" in mise.toml." >&2
  exit 1
fi

if [ -z "$command" ]; then
  echo "$pin"
  exit 0
fi

found=$("$command" --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1) || true
if [ "$found" != "$pin" ]; then
  echo "$command ${found:-is not on PATH}; this repository pins $pin (mise.toml)." >&2
  echo "  mise install    — or mise run doctor for everything else" >&2
  exit 1
fi

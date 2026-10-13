#!/usr/bin/env bash
# The pre-push hook: configure, build and test on the linux-clang preset
# (macos-clang on a Mac), when there is an engine to build against.
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$HOME/.cargo/bin:$PATH"
if [ -z "${RIPPLE_ENGINE_TREE:-}" ] && [ "$(jq -r '.archives.conformance.sha256' engine.json)" = null ]; then
  echo "pre-push: engine.json pins no release and RIPPLE_ENGINE_TREE is unset; skipped the build." >&2
  exit 0
fi
case "$(uname -s)" in
  Darwin) preset=macos-clang ;;
  *) preset=linux-clang ;;
esac
cmake --preset "$preset" > /dev/null
cmake --build --preset "$preset"
ctest --preset "$preset"

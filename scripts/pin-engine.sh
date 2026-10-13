#!/usr/bin/env bash
# Pins an engine release in engine.json, or checks that the committed pin is
# one. The one place a signature is checked: CMake trusts the pin's digests at
# configure time and cannot verify Ed25519 itself, so this is run by a
# maintainer when the pin moves, and by CI on every check.
#
#   scripts/pin-engine.sh <version>     download the release's manifest, verify
#                                       it against engine-signing.pub, and
#                                       write every C archive's digest into
#                                       engine.json
#   scripts/pin-engine.sh --verify      the same check over the committed pin,
#                                       failing if any digest is not the
#                                       signed manifest's
#
# Needs curl, jq and OpenSSL 3 (Ed25519 with `pkeyutl -rawin`).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pin="$root/engine.json"
key="$root/engine-signing.pub"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

die() { echo "pin-engine: $*" >&2; exit 1; }
for tool in curl jq openssl; do
  command -v "$tool" >/dev/null || die "$tool is not installed"
done

case "${1:-}" in
  --verify) version="$(jq -r .engine "$pin")"; verify=1 ;;
  ''|-*) die "usage: $0 <version> | --verify" ;;
  *) version="$1"; verify=0 ;;
esac
tag="engine-v$version"
contract="${version%.*}"

# The first of the pin's URLs that answers.
fetch() {
  local file=$1 out=$2 template url
  while read -r template; do
    url="${template//\{tag\}/$tag}"
    url="${url//\{file\}/$file}"
    if curl -fsSL --retry 2 -o "$out" "$url"; then return 0; fi
  done < <(jq -r '.urls[]' "$pin")
  die "could not download $file for $tag"
}

fetch engine-manifest.json "$work/manifest.json"
fetch engine-manifest.json.sig "$work/manifest.json.sig"

# The raw 32-byte public key, wrapped as the SubjectPublicKeyInfo OpenSSL
# reads: a fixed twelve-byte DER prefix for Ed25519, then the key.
{ printf '302a300506032b6570032100' | xxd -r -p; base64 -d < "$key"; } > "$work/key.der"
openssl pkey -pubin -inform DER -in "$work/key.der" -out "$work/key.pem" 2>/dev/null \
  || die "engine-signing.pub is not an Ed25519 public key"
base64 -d < "$work/manifest.json.sig" > "$work/manifest.sig"
openssl pkeyutl -verify -pubin -inkey "$work/key.pem" -rawin \
  -in "$work/manifest.json" -sigfile "$work/manifest.sig" >/dev/null 2>&1 \
  || die "the manifest's signature does not verify against engine-signing.pub"

key_id="$(base64 -d < "$key" | openssl dgst -sha256 -r | cut -c1-16)"
manifest() { jq -r "$@" "$work/manifest.json"; }
[ "$(manifest .keyId)" = "$key_id" ] || die "the manifest names key $(manifest .keyId), not $key_id"
[ "$(manifest .engine)" = "$version" ] || die "the manifest is engine $(manifest .engine), not $version"
[ "$(manifest .contract)" = "$contract" ] || die "the manifest is contract $(manifest .contract)"
[ "$(manifest '.capi.hash // empty')" != "" ] \
  || die "engine $version carries no C ABI (the manifest has no \`capi\`): the first that does is 1.5.1"

# Every native target's C archive, both variants, and the fixtures.
pinned="$(jq --arg key "$key_id" '
  input as $pin
  | {
    "$comment": $pin["$comment"],
    engine: .engine,
    contract: .contract,
    keyId: $key,
    urls: $pin.urls,
    archives: {
      python: (.variants.python | with_entries(select(.value.c) | .value = {file: .value.c.archive, sha256: .value.c.sha256})),
      "no-python": (.variants["no-python"] | with_entries(select(.value.c) | .value = {file: .value.c.archive, sha256: .value.c.sha256})),
      conformance: {file: .conformance.archive, sha256: .conformance.sha256}
    }
  }' "$work/manifest.json" "$pin")"

if [ "$verify" = 1 ]; then
  diff <(jq -S .archives <<< "$pinned") <(jq -S .archives "$pin") >/dev/null \
    || die "engine.json's digests are not the signed manifest's for $tag"
  [ "$(jq -r .keyId "$pin")" = "$key_id" ] || die "engine.json names another key"
  echo "pin-engine: engine.json is $tag, as signed."
else
  printf '%s\n' "$pinned" > "$pin"
  echo "pin-engine: engine.json now pins $tag ($(jq '[.archives.python, .archives["no-python"]] | map(length) | add' "$pin") archives)."
fi

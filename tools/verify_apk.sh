#!/usr/bin/env bash
# Fails if an APK carries game content, keys, an unexpected ABI, or an
# unexpectedly large entry. usage: verify_apk.sh <apk> [expected-lib]
set -euo pipefail

APK="${1:?usage: verify_apk.sh <apk> [expected-lib]}"
EXPECTED_LIB="${2:-}"
fail=0
note() { printf '  \033[31mFAIL\033[0m %s\n' "$*"; fail=1; }
ok()   { printf '  \033[32m ok \033[0m %s\n' "$*"; }

echo "verify: $APK"
test -s "$APK" || { note "APK missing or empty"; exit 1; }

listing=$(unzip -l "$APK")

if echo "$listing" | grep -Ei '\.(nsp|nsz|xci|xcz|nca|ncz|nup|keys|tik|cert)([[:space:]]|$)'; then
  note "APK contains a game or key file (see above)"
else
  ok "no game/key file extensions"
fi

if echo "$listing" | grep -Ei '(^|/)(prod\.keys|title\.keys|hl\.nsp)$'; then
  note "APK contains a forbidden file name"
else
  ok "no forbidden file names"
fi

# unexpected entries above 100 MB (the app's own native library is ~0.3 MB)
while read -r size name; do
  if [ -n "${size:-}" ] && [ "$size" -gt 104857600 ] 2>/dev/null; then
    note "entry over 100 MB: $name ($size bytes)"
  fi
done < <(echo "$listing" | awk 'NF>=4 && $1 ~ /^[0-9]+$/ {print $1, $4}')

abis=$(echo "$listing" | awk '/^ *[0-9]+ .*lib\/[^/]+\/.*\.so$/ {print $4}' | cut -d/ -f2 | sort -u)
if [ -z "$abis" ]; then
  note "APK has no native libraries"
else
  echo "  ABIs: $(echo "$abis" | tr '\n' ' ')"
  if [ "$abis" != "arm64-v8a" ]; then
    note "expected only arm64-v8a"
  else
    ok "arm64-v8a only"
  fi
fi

if [ -n "$EXPECTED_LIB" ]; then
  if echo "$listing" | grep -q "lib/arm64-v8a/$EXPECTED_LIB"; then
    ok "$EXPECTED_LIB present"
  else
    note "$EXPECTED_LIB missing"
  fi
fi

if command -v aapt >/dev/null 2>&1; then
  aapt dump badging "$APK" | grep -E "^(package|sdkVersion|targetSdkVersion|native-code)" || true
fi

if [ "$fail" -ne 0 ]; then
  echo "verify: FAILED"
  exit 1
fi
echo "verify: clean"

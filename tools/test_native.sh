#!/usr/bin/env bash
# Builds and runs the host unit test for the native core. No Android SDK,
# no device and no game files are needed.
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-c++}"
OUT="${TMPDIR:-/tmp}/nx2a-native-test"
mkdir -p "$OUT"

"$CXX" -std=c++20 -O1 -Wall -Wextra -Werror -Iapp/src/main/cpp \
    tests/native/test_nsp.cpp \
    app/src/main/cpp/keys.cpp \
    app/src/main/cpp/nsp.cpp \
    app/src/main/cpp/import_check.cpp \
    app/src/main/cpp/crypto.cpp \
    app/src/main/cpp/content_check.cpp \
    app/src/main/cpp/nca.cpp \
    -o "$OUT/test_nsp"
"$OUT/test_nsp"

# The probe is built here so it cannot rot; it is only run manually against
# a user's own NSP (never in CI).
"$CXX" -std=c++20 -O1 -Wall -Wextra -Werror -Iapp/src/main/cpp \
    tests/native/probe_nsp.cpp \
    app/src/main/cpp/keys.cpp \
    app/src/main/cpp/nsp.cpp \
    app/src/main/cpp/import_check.cpp \
    -o "$OUT/probe_nsp"

"$CXX" -std=c++20 -O1 -Wall -Wextra -Werror -Iapp/src/main/cpp \
    tests/native/probe_content.cpp \
    app/src/main/cpp/keys.cpp \
    app/src/main/cpp/nsp.cpp \
    app/src/main/cpp/crypto.cpp \
    app/src/main/cpp/content_check.cpp \
    app/src/main/cpp/nca.cpp \
    -o "$OUT/probe_content"

"$CXX" -std=c++20 -O1 -Wall -Wextra -Werror -Iapp/src/main/cpp \
    tests/native/probe_romfs.cpp \
    app/src/main/cpp/keys.cpp \
    app/src/main/cpp/nsp.cpp \
    app/src/main/cpp/crypto.cpp \
    app/src/main/cpp/content_check.cpp \
    app/src/main/cpp/nca.cpp \
    -o "$OUT/probe_romfs"

echo "native core: all checks passed ($OUT)"

#!/usr/bin/env bash
# Audit before every push. Fails (exit 1) on anything that could ship game
# content, keys, or unexpectedly large files.
set -uo pipefail
cd "$(dirname "$0")/.."

fail=0
note() { printf '  \033[31mFAIL\033[0m %s\n' "$*"; fail=1; }
ok()   { printf '  \033[32m ok \033[0m %s\n' "$*"; }

echo "audit: game content and keys"

# 1. tracked files with a forbidden extension
bad_ext=$(git ls-files | grep -Ei '\.(nsp|nsz|xci|xcz|nca|ncz|nup|keys|zip|tik|cert)$' || true)
if [ -n "$bad_ext" ]; then
  note "tracked files with a game/key extension:"; echo "$bad_ext" | sed 's/^/         /'
else
  ok "no tracked game or key extensions"
fi

# 2. forbidden file names anywhere in the tree (tracked or untracked)
bad_names=$(git ls-files | grep -Ei '(^|/)(prod\.keys|title\.keys|hl\.nsp)$' || true)
if [ -n "$bad_names" ]; then
  note "tracked key/ROM file names:"; echo "$bad_names" | sed 's/^/         /'
else
  ok "no tracked prod.keys / title.keys / hl.nsp"
fi

# 3. any tracked file larger than 50 MB
big=$(git ls-files -z | xargs -0 -r -n1 sh -c 'test -f "$0" && s=$(wc -c <"$0") && test "$s" -gt 52428800 && echo "$s $0"' 2>/dev/null || true)
if [ -n "$big" ]; then
  note "tracked files over 50 MB:"; echo "$big" | sed 's/^/         /'
else
  ok "no tracked file over 50 MB"
fi

# 4. key material in tracked text
if git grep -nIE '^(master_key|header_key|key_area_key|titlekek)[a-z_0-9]*[[:space:]]*=[[:space:]]*[0-9a-fA-F]{32}' -- . >/dev/null 2>&1; then
  note "key material (name = 32 hex chars) found in tracked files:"
  git grep -nIE '^(master_key|header_key|key_area_key|titlekek)[a-z_0-9]*[[:space:]]*=[[:space:]]*[0-9a-fA-F]{32}' -- . | cut -c1-120
else
  ok "no key material in tracked files"
fi

# 5. a local game/key folder must never be tracked
if git ls-files | grep -qE '(^|/)(local_assets|\.gamedump)/'; then
  note "local_assets/ or .gamedump/ is tracked"
else
  ok "local game folders are untracked"
fi

# 6. current working tree: any game file that is not ignored is suspicious
if git status --porcelain --untracked-files=all | grep -Ei '\.(nsp|nsz|xci|xcz|nca|ncz|nup|keys|tik|zip)$'; then
  note "an untracked game/key file is not covered by .gitignore (see above)"
else
  ok "no unignored game/key files in the working tree"
fi

if [ "$fail" -ne 0 ]; then
  echo "audit: FAILED — do not push"
  exit 1
fi
echo "audit: clean"

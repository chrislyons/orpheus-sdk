#!/usr/bin/env bash
set -euo pipefail

# Ensure we have a local reference to origin/main so changeset status can diff
if git remote get-url origin >/dev/null 2>&1; then
  git remote set-branches --add origin main || true
  git fetch --no-tags --prune --progress origin main:refs/remotes/origin/main
  export CHANGESET_BASE=origin/main
else
  echo '→ No origin remote configured; using default changeset base'
  export CHANGESET_BASE=$(git config --get changeset-base || echo main)
fi

# Prefer the actual remote main; fall back to default if needed

# Print useful context
echo "→ Running changeset status against ${CHANGESET_BASE}"
if ! pnpm changeset status --since="${CHANGESET_BASE}"; then
  echo '→ Changeset status command failed (non-blocking)'
fi

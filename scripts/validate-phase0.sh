#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "$REPO_ROOT"

echo "=== Phase 0 Validation ==="

echo ""
echo "→ Checking monorepo structure..."
[ -f pnpm-workspace.yaml ]
[ -d packages ]

echo ""
echo "→ Verifying dependency installation..."

# Ensure pnpm@8 is available (CI and local parity)
if ! command -v pnpm >/dev/null 2>&1; then
  echo "→ Installing pnpm@8.15.4 (local)"
  npm i -g pnpm@8.15.4
else
  echo "→ Detected pnpm: $(pnpm -v)"
fi

# Guard against lockfile mismatch: if frozen install fails, explain and exit 1
echo "→ Installing dependencies with frozen lockfile"
set +e
pnpm install --frozen-lockfile
rc=$?
set -e
if [ $rc -ne 0 ]; then
  echo "✗ pnpm frozen install failed (likely lockfile/version mismatch)."
  echo "  Fix by running locally with pnpm@8.15.4:"
  echo "    npm i -g pnpm@8.15.4 && pnpm install && git add pnpm-lock.yaml && git commit -m \"chore: regen lockfile for pnpm@8\""
  exit 1
fi
echo "✓ Dependencies installed (frozen)"

pnpm list --depth 0

echo ""
echo "→ Verifying workspace configuration..."
cat pnpm-workspace.yaml

echo ""
echo "→ Verifying C++ build..."
if [ ! -d build ]; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
fi
cmake --build build

echo ""
echo "→ Running C++ tests..."
ctest --test-dir build --output-on-failure

echo ""
echo "→ Running lint suite..."
pnpm run lint

echo ""
# Optional: report changesets status (non-blocking in Phase 0)
if [ -f ".changeset/config.json" ]; then
  echo "→ Checking Changesets status (non-blocking)"
  set +e
  ./scripts/changesets-status.sh
  set -e
else
  echo "→ Changesets not configured yet (skipping status)"
fi

echo ""
echo "✅ Phase 0 validation complete!"

#!/usr/bin/env bash
set -euo pipefail

echo "🧹 Codex Cloud housekeeping (CI-safe)"

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

PRIMARY_DOC="docs/integration/ORP068 Implementation Plan v2.0_ Orpheus SDK × Shmui Integration .md"

if [ -f "$PRIMARY_DOC" ]; then
  echo "→ Found authoritative plan: $PRIMARY_DOC"
else
  echo "ℹ️  ORP068 not found (expected if not committed yet)"
fi

# --- Skip Git hooks entirely in CI ----------------------------------------
if [ "${CI:-}" = "true" ]; then
  echo "ℹ️  CI detected — skipping local hook setup."
else
  git config core.hooksPath .githooks
  mkdir -p .githooks

  cat > .githooks/pre-commit <<'HOOK'
#!/usr/bin/env bash
set -euo pipefail
if [ "${CI:-}" = "true" ] || [ "${ALLOW_BINARIES:-}" = "true" ]; then exit 0; fi
ALLOWLIST_REGEX='^(docs/images/.*\.(png|jpe?g|svg)$|packages/contract/fixtures/golden-audio/.*\.wav$)'
is_binary_ext(){ case "${1,,}" in *.wasm|*.node|*.so|*.dll|*.dylib|*.exe|*.zip|*.tar|*.gz|*.mp3|*.wav|*.png|*.jpg|*.jpeg|*.gif) return 0;; *) return 1;; esac; }
mapfile -t FILES < <(git diff --cached --name-only --diff-filter=ACMR)
BLOCKED=()
for f in "${FILES[@]}"; do [ -f "$f" ] || continue; [[ "$f" =~ $ALLOWLIST_REGEX ]] && continue; is_binary_ext "$f" && BLOCKED+=("$f"); done
if [ "${#BLOCKED[@]}" -gt 0 ]; then
  echo "❌ Commit blocked: binary files not allowed."
  for b in "${BLOCKED[@]}"; do echo "   - $b"; done
  echo "Override: ALLOW_BINARIES=true git commit ..."
  exit 1
fi
HOOK
  chmod +x .githooks/pre-commit
  echo "✓ Local pre-commit binary guard installed"
fi

# --- Codex Cloud hint file -------------------------------------------------
if ! git ls-files --error-unmatch .codexcloudrc.json >/dev/null 2>&1 && [ ! -f .codexcloudrc.json ]; then
  cat > .codexcloudrc.json <<EOF2
{
  "project": "orpheus-sdk",
  "primaryDoc": "$PRIMARY_DOC",
  "taskIdFormat": "{PHASE}.{DOMAIN}.{SEQUENCE}",
  "bootstrap": "scripts/bootstrap-dev.sh",
  "phaseValidators": {
    "P0": "scripts/validate-phase0.sh",
    "P1": "scripts/validate-phase1.sh",
    "P2": "scripts/validate-phase2.sh",
    "P3": "scripts/validate-phase3.sh",
    "P4": "scripts/validate-phase4.sh"
  }
}
EOF2
  echo "✓ Wrote local .codexcloudrc.json (untracked)"
else
  echo "ℹ️  .codexcloudrc.json already exists or tracked; leaving as-is."
fi

echo "✅ Housekeeping complete (no installs, no builds)."

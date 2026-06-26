#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "==> Building frontend..."
cd "$SCRIPT_DIR/client"
bun run build.ts

echo "==> Starting server..."
cd "$SCRIPT_DIR/web"
exec go run .

#!/usr/bin/env bash
set -euo pipefail

echo "==> Pulling latest..."
git pull origin main

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "==> Building frontend..."
cd "$SCRIPT_DIR/client"
bun run build.ts

echo "==> Replacing dist..."
rm -rf "$SCRIPT_DIR/web/dist"
mv "$SCRIPT_DIR/client/dist" "$SCRIPT_DIR/web/dist"

echo "==> Starting server..."
cd "$SCRIPT_DIR/web"
exec go run .



# docker build -t webserver .
# docker run -p 8080:8080 webserver

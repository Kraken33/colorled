#!/bin/bash
# Start LedFx in background and open web interface
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if ! pgrep -f "ledfx" > /dev/null; then
    echo "Starting LedFx on http://localhost:8888 ..."
    "$DIR/ledfx_venv/bin/ledfx" --host 0.0.0.0 --port 8888 > /dev/null 2>&1 &
    sleep 2
fi
open "http://localhost:8888"


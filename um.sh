#!/bin/bash

# ESP32 Upload & Monitor Script
# Convenience script for upload and monitor cycle
# Usage: ./um.sh [board-name] [port]
#   - board-name: Required when multiple boards configured
#   - port: Optional, auto-detected if not provided

set -e

# Load common configuration and functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/config.sh"

echo -e "${CYAN}=== ESP32 Upload & Monitor ===${NC}"
echo ""

# Parse and validate board and port arguments
parse_board_and_port_args "$@"

# Run upload → monitor
"$SCRIPT_DIR/upload.sh" "$BOARD" "$PORT"
echo ""
"$SCRIPT_DIR/monitor.sh" "$PORT"

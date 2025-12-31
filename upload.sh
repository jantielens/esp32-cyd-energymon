#!/bin/bash

# ESP32 Upload Script
# Uploads compiled firmware to ESP32 via serial port
# Usage: ./upload.sh [board-name] [port]
#   - board-name: Required when multiple boards configured
#   - port: Optional, auto-detected if not provided

set -e

# Load common configuration and functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/config.sh"

# Get arduino-cli path
ARDUINO_CLI=$(find_arduino_cli)

# Parse board and port arguments
parse_board_and_port_args "$@"

# Board-specific build directory
BOARD_BUILD_PATH="$BUILD_PATH/$BOARD"

# Auto-detect port if not specified
if [[ -z "$PORT" ]]; then
    if PORT=$(find_serial_port); then
        echo -e "${GREEN}Auto-detected port: $PORT${NC}"
    else
        echo -e "${RED}Error: No serial port detected${NC}"
        if [[ "${#FQBN_TARGETS[@]}" -gt 1 ]]; then
            echo "Usage: ${0##*/} <board-name> [port]"
            echo "Example: ${0##*/} $BOARD /dev/ttyUSB0"
        else
            echo "Usage: ${0##*/} [port]"
            echo "Example: ${0##*/} /dev/ttyUSB0"
        fi
        exit 1
    fi
fi

echo -e "${CYAN}=== Uploading ESP32 Firmware ===${NC}"

# Check if board build directory exists
if [ ! -d "$BOARD_BUILD_PATH" ]; then
    echo -e "${RED}Error: Build directory not found for board '$BOARD'${NC}"
    echo "Expected: $BOARD_BUILD_PATH"
    echo "Please run: ./build.sh $BOARD"
    exit 1
fi

# Display upload configuration
echo "Board: $BOARD"
echo "FQBN:  $FQBN"
echo "Port:  $PORT"
echo "Build: $BOARD_BUILD_PATH"
echo ""

# Upload firmware
"$ARDUINO_CLI" upload \
    --fqbn "$FQBN" \
    --port "$PORT" \
    --input-dir "$BOARD_BUILD_PATH"

echo ""
echo -e "${GREEN}=== Upload Complete ===${NC}"
echo "Run ./monitor.sh to view serial output"
